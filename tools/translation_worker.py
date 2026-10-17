import argparse
import json
import os
import re
import sys
from collections import OrderedDict
from pathlib import Path
from time import perf_counter


DEFAULT_MODEL = (
    Path(__file__).resolve().parents[1]
    / "models"
    / "translation"
    / "en_vi"
)


class OfflineTranslator:
    def __init__(self, model_dir):
        import ctranslate2
        import sentencepiece

        model_dir = Path(model_dir)

        metadata = json.loads(
            (model_dir / "metadata.json").read_text(encoding="utf-8")
        )

        languages = (
            metadata.get("from_code"),
            metadata.get("to_code"),
        )

        if languages != ("en", "vi"):
            raise ValueError("Mo hinh khong phai Anh -> Viet.")

        self.tokenizer = sentencepiece.SentencePieceProcessor(
            model_file=str(model_dir / "sentencepiece.model")
        )

        self.engine = ctranslate2.Translator(
            str(model_dir / "model"),
            device="cpu",
            compute_type="int8",
            inter_threads=1,
            intra_threads=max(
                1,
                min(4, (os.cpu_count() or 2) // 2),
            ),
        )

        # Bộ nhớ đệm chỉ tồn tại trong phiên chạy worker.
        self._translation_cache = OrderedDict()
        self._cache_limit = 2000

    @staticmethod
    def _word_boundary(tokens, limit):
        limit = min(limit, len(tokens))

        if limit == len(tokens):
            return limit

        for position in range(limit, max(0, limit // 2), -1):
            if tokens[position].startswith("▁"):
                return position

        return limit

    def _prepare_line(self, line):
        # Dịch từng câu rồi ghép lại đúng đoạn ban đầu.
        sentences = re.split(r"(?<=[.!?])\s+", line.strip())
        chunks = []

        for sentence in sentences:
            sentence = sentence.strip()

            if not sentence:
                continue

            tokens = self.tokenizer.encode(
                sentence,
                out_type=str,
            )

            # Chỉ chia nhỏ thêm nếu một câu quá dài.
            while len(tokens) > 96:
                cut = self._word_boundary(tokens, 96)
                chunks.append(tokens[:cut])
                tokens = tokens[cut:]

            if tokens:
                chunks.append(tokens)

        return chunks

    def _translate_chunks(self, chunks, depth=0):
        if not chunks:
            return []

        results = self.engine.translate_batch(
            chunks,
            beam_size=2,
            max_batch_size=8,
            max_input_length=0,
            max_decoding_length=512,
            repetition_penalty=1.05,
            no_repeat_ngram_size=6,
            return_end_token=True,
            replace_unknowns=True,
        )

        if len(results) != len(chunks):
            raise RuntimeError("So ket qua dich khong khop.")

        outputs = []

        for tokens, result in zip(chunks, results):
            pieces = (
                result.hypotheses[0]
                if result.hypotheses
                else []
            )

            output = ""

            if pieces and pieces[-1] == "</s>":
                output = self.tokenizer.decode(pieces[:-1])

                # Không để xuống dòng phát sinh làm lệch overlay.
                output = " ".join(
                    output.replace("▁", " ").split()
                )

            if output:
                outputs.append(output)
                continue

            # Chia nhỏ phần chưa dịch được rồi thử lại.
            if depth < 3 and len(tokens) > 1:
                cut = self._word_boundary(
                    tokens,
                    len(tokens) // 2,
                )

                print(
                    f"[translation] Retry: {len(tokens)} tokens, "
                    f"depth={depth + 1}",
                    file=sys.stderr,
                    flush=True,
                )

                parts = self._translate_chunks(
                    [tokens[:cut], tokens[cut:]],
                    depth + 1,
                )

                outputs.append(" ".join(parts))
                continue

            # Giữ nội dung gốc và đánh dấu nếu vẫn không dịch được.
            source = self.tokenizer.decode(tokens)
            source = " ".join(
                source.replace("▁", " ").split()
            )

            outputs.append(f"[Chưa dịch: {source}]")

            print(
                "[translation] Untranslated fragment after retries.",
                file=sys.stderr,
                flush=True,
            )

        return outputs

    def _translate_cached(self, chunks):
        if not chunks:
            return []

        started = perf_counter()
        outputs = [""] * len(chunks)
        pending = {}
        cache_hits = 0

        for index, tokens in enumerate(chunks):
            # Khóa là chính chuỗi token gửi cho mô hình.
            key = tuple(tokens)

            if key in self._translation_cache:
                outputs[index] = self._translation_cache[key]
                self._translation_cache.move_to_end(key)
                cache_hits += 1
            else:
                # Câu trùng trong cùng yêu cầu chỉ dịch một lần.
                pending.setdefault(key, []).append(index)

        if pending:
            keys = list(pending)

            fresh = self._translate_chunks(
                [list(key) for key in keys]
            )

            if len(fresh) != len(keys):
                raise RuntimeError("So ket qua cache khong khop.")

            for key, output in zip(keys, fresh):
                for index in pending[key]:
                    outputs[index] = output

                # Không lưu phần dịch thất bại.
                if output and "[Chưa dịch:" not in output:
                    self._translation_cache[key] = output
                    self._translation_cache.move_to_end(key)

                    # Loại mục lâu không được sử dụng nhất khi đầy.
                    while (
                        len(self._translation_cache) > self._cache_limit
                    ):
                        self._translation_cache.popitem(last=False)

        duplicates = len(chunks) - cache_hits - len(pending)
        elapsed_ms = (perf_counter() - started) * 1000

        # Log ở stderr, không lẫn vào JSON gửi cho C++.
        print(
            f"[cache] hits={cache_hits} "
            f"new={len(pending)} "
            f"duplicates={duplicates} "
            f"stored={len(self._translation_cache)} "
            f"elapsed_ms={elapsed_ms:.1f}",
            file=sys.stderr,
            flush=True,
        )

        return outputs

    def translate(self, texts):
        if not isinstance(texts, list) or not 1 <= len(texts) <= 128:
            raise ValueError(
                "Can danh sach tu 1 den 128 doan van ban."
            )

        if any(not isinstance(text, str) for text in texts):
            raise ValueError("Moi phan tu phai la chuoi.")

        if sum(len(text) for text in texts) > 32768:
            raise ValueError(
                "Van ban qua dai; hay chia nho yeu cau."
            )

        inputs = []
        layouts = []

        for text in texts:
            lines = []

            for line in text.split("\n"):
                indices = []

                for chunk in self._prepare_line(line):
                    indices.append(len(inputs))
                    inputs.append(chunk)

                lines.append(indices)

            layouts.append(lines)

        translated = self._translate_cached(inputs)

        # Giữ nguyên số dòng/khối để khớp tọa độ overlay.
        return [
            "\n".join(
                " ".join(translated[i] for i in indices)
                for indices in lines
            )
            for lines in layouts
        ]


def emit(data):
    print(
        json.dumps(data, ensure_ascii=False),
        flush=True,
    )


def main():
    for stream in (sys.stdin, sys.stdout, sys.stderr):
        stream.reconfigure(encoding="utf-8")

    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--model",
        type=Path,
        default=DEFAULT_MODEL,
    )

    parser.add_argument(
        "--self-test",
        action="store_true",
    )

    args = parser.parse_args()

    try:
        translator = OfflineTranslator(args.model)
    except Exception as error:
        emit({
            "type": "error",
            "error": f"Khong nap duoc bo dich: {error}",
        })
        return 1

    if args.self_test:
        texts = [
            "Hello everyone.",
            "Please open the settings window.",
            "This is a screen translation test.",
        ]

        try:
            results = translator.translate(texts)

            for source, target in zip(texts, results):
                print(f"EN: {source}\nVI: {target}\n")

        except Exception as error:
            emit({
                "type": "error",
                "error": str(error),
            })
            return 1

        return 0

    emit({
        "type": "ready",
        "source": "en",
        "target": "vi",
    })

    while True:
        raw = sys.stdin.buffer.readline(262145)

        if not raw:
            return 0

        if len(raw) > 262144:
            emit({
                "type": "error",
                "error": "Yeu cau qua lon.",
            })
            return 2

        request_id = None

        try:
            request = json.loads(raw.decode("utf-8"))

            if not isinstance(request, dict):
                raise ValueError(
                    "Yeu cau phai la JSON object."
                )

            request_id = request.get("id")

            if not isinstance(request_id, str) or not request_id:
                raise ValueError("Thieu id dang chuoi.")

            languages = (
                request.get("source"),
                request.get("target"),
            )

            if languages != ("en", "vi"):
                raise ValueError(
                    "Phien ban nay chi ho tro Anh -> Viet."
                )

            translations = translator.translate(
                request.get("texts")
            )

            emit({
                "type": "result",
                "id": request_id,
                "translations": translations,
            })

        except Exception as error:
            emit({
                "type": "error",
                "id": request_id,
                "error": str(error),
            })


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(0)