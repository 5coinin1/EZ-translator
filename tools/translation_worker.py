import argparse
import json
import os
from pathlib import Path
import sys

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
                tokens = self.tokenizer.encode(
                    line.strip(),
                    out_type=str,
                )

                indices = []

                while tokens:
                    end = min(192, len(tokens))

                    if end < len(tokens):
                        # Ưu tiên cắt tại đầu từ.
                        for boundary in range(end, end // 2, -1):
                            if tokens[boundary].startswith("▁"):
                                end = boundary
                                break

                    indices.append(len(inputs))
                    inputs.append(tokens[:end])
                    tokens = tokens[end:]

                lines.append(indices)

            layouts.append(lines)

        translated = []

        if inputs:
            results = self.engine.translate_batch(
                inputs,
                beam_size=2,
                max_batch_size=8,
                max_input_length=0,
                max_decoding_length=512,
                return_end_token=True,
                replace_unknowns=True,
            )

            for result in results:
                pieces = result.hypotheses[0]

                if not pieces or pieces[-1] != "</s>":
                    raise RuntimeError(
                        "Ban dich vuot gioi han; "
                        "hay chia doan ngan hon."
                    )

                output = self.tokenizer.decode(pieces[:-1])

                # Loại dấu phân cách còn lại trong đầu ra mô hình.
                output = output.replace("▁", " ").strip()

                if not output:
                    raise RuntimeError(
                        "Mo hinh tra ve ban dich rong."
                    )

                translated.append(output)

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