"""Sidecar dịch offline (CTranslate2) cho EZTranslator.

Giao thức: mỗi dòng 1 JSON trên stdin, trả 1 dòng JSON trên stdout.

    -> {"texts": ["Open the door", "Where is the key?"]}
    <- {"translations": ["Mở cửa ra", "Chìa khóa đâu?"]}

Chạy:
    py tools/ct2_server.py <model_dir> [compute_type] [intra_threads] [src_lang] [tgt_lang]

LƯU Ý (Windows): KHÔNG đặt shebang "#!/usr/bin/env python3" ở đầu file. `py.exe`
đọc shebang rồi chạy `python3` trên PATH — thường là bản khác (vd msys2) không có
ctranslate2, và sẽ báo "No module named 'ctranslate2'" dù `py -c "import
ctranslate2"` vẫn chạy được.

Tự nhận loại model (thả model CTranslate2 nào vào cũng chạy):

  * "marian" (Helsinki opus-mt, từng cặp): tokenizer là source.spm / target.spm,
    chuỗi nguồn kết thúc bằng '</s>'.
  * "nllb" (Meta NLLB-200): tokenizer là sentencepiece.bpe.model, nguồn có tiền tố
    mã ngôn ngữ nguồn (vd eng_Latn) và đích đặt bằng target_prefix (vd vie_Latn).

Bền: câu quá dài bị cắt tại dấu câu (dưới giới hạn vị trí của model); một câu lỗi
trả về rỗng và không làm hỏng các câu anh em.
"""
import json
import os
import re
import sys

import ctranslate2
import sentencepiece as spm

# Marian mã hoá vị trí dừng ở 512 token; giữ biên an toàn và chặn luôn độ trễ.
MAX_SRC_TOKENS = 400

# beam 4: chất lượng tốt hơn hẳn greedy và rẻ (đo được trên máy này còn NHANH hơn
# beam 1 vì câu lặp vô hạn không còn bị đẩy tới hết max token). Chỉnh bằng env khi A/B.
BEAM = int(os.environ.get("EZ_CT2_BEAM", "4"))
MAX_DECODING = int(os.environ.get("EZ_CT2_MAX_TOKENS", "256"))

_SPLIT_RE = re.compile(r"(?<=[.!?…。！？；;])\s*")


def load_spm(path):
    processor = spm.SentencePieceProcessor()
    if not processor.Load(path):
        raise RuntimeError(f"không nạp được SentencePiece model: {path}")
    return processor


class Engine:
    """Một model CTranslate2 + tokenizer tương ứng."""

    def __init__(self, model_dir, compute_type, intra_threads, src_lang, tgt_lang):
        self.src_lang = src_lang
        self.tgt_lang = tgt_lang

        nllb_spm = os.path.join(model_dir, "sentencepiece.bpe.model")
        if os.path.exists(nllb_spm):
            self.kind = "nllb"
            self.sp_shared = load_spm(nllb_spm)
        else:
            self.kind = "marian"
            self.sp_src = load_spm(os.path.join(model_dir, "source.spm"))
            self.sp_tgt = load_spm(os.path.join(model_dir, "target.spm"))

        self.translator = ctranslate2.Translator(
            model_dir,
            device="cpu",
            compute_type=compute_type,
            inter_threads=1,
            intra_threads=max(1, intra_threads),
        )

    # ── Tokenize / detokenize ────────────────────────────────────────────────
    def _encode(self, text):
        # LƯU Ý: sentencepiece >= 0.2 trả id mặc định → phải ép out_type=str.
        if self.kind == "nllb":
            return [self.src_lang] + self.sp_shared.encode(text, out_type=str)
        return self.sp_src.encode(text, out_type=str) + ["</s>"]

    def _decode(self, tokens):
        skip = {"</s>", "<s>", "<pad>"}
        pieces = [t for t in tokens if t not in skip]
        if self.kind == "nllb":
            pieces = [t for t in pieces if t != self.src_lang]
            return self.sp_shared.decode(pieces).strip()
        return self.sp_tgt.decode(pieces).strip()

    def _target_prefix(self):
        return [[self.tgt_lang]] if self.kind == "nllb" and self.tgt_lang else None

    # ── Dịch ────────────────────────────────────────────────────────────────
    def _translate_batch(self, texts):
        tokens = [self._encode(t) for t in texts]
        results = self.translator.translate_batch(
            tokens,
            target_prefix=self._target_prefix(),
            beam_size=BEAM,
            max_decoding_length=MAX_DECODING,
        )
        return [self._decode(r.hypotheses[0]) for r in results]

    def translate(self, text):
        """Dịch một đơn vị; cắt câu quá dài thành nhiều mảnh để không bị cụt."""
        pieces = [p for p in _SPLIT_RE.split(text) if p.strip()] or [text]
        chunks, current = [], ""
        # Gom mảnh lại cho tới khi gần chạm giới hạn token của model.
        for piece in pieces:
            candidate = (current + " " + piece).strip() if current else piece
            if len(self._encode(candidate)) <= MAX_SRC_TOKENS or not current:
                current = candidate
            else:
                chunks.append(current)
                current = piece
        if current:
            chunks.append(current)

        try:
            return " ".join(self._translate_batch(chunks)).strip()
        except Exception as exc:  # một câu lỗi không được làm rỗng cả trang
            print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)
            return ""


def main():
    if len(sys.argv) < 2:
        print("usage: ct2_server.py <model_dir> [compute_type] [threads] [src] [tgt]",
              file=sys.stderr)
        return 2

    # Ghi UTF-8 ra stdout bất kể codepage console của Windows.
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stdin.reconfigure(encoding="utf-8")

    model_dir = sys.argv[1]
    compute_type = sys.argv[2] if len(sys.argv) > 2 else "int8"
    threads = int(sys.argv[3]) if len(sys.argv) > 3 else max(1, (os.cpu_count() or 4) // 2)
    src_lang = sys.argv[4] if len(sys.argv) > 4 else ""
    tgt_lang = sys.argv[5] if len(sys.argv) > 5 else ""

    try:
        engine = Engine(model_dir, compute_type, threads, src_lang, tgt_lang)
    except Exception as exc:
        print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)
        return 1

    print(json.dumps({"ready": True, "kind": engine.kind}), flush=True)

    for raw in sys.stdin:
        raw = raw.strip()
        if not raw:
            continue
        try:
            request = json.loads(raw)
        except ValueError:
            continue
        if "texts" not in request:
            continue
        texts = [str(t) for t in request["texts"]]
        print(json.dumps({"translations": [engine.translate(t) for t in texts]}), flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
