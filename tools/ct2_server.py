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

FAST PATH (bật mặc định, tắt bằng EZ_CT2_FAST=0):
  - Tách câu, mỗi mảnh <= EZ_CT2_FAST_TOKENS token (mặc định 96) thay vì gom cả
    đoạn tới 400 token. Decode ngắn hơn rất nhiều nên đoạn dài nhanh hơn ~3x.
  - Batch TOÀN BỘ câu của mọi text trong một request vào một translate_batch
    (beam 2, max_batch_size 8) thay vì dịch từng text tuần tự.
  - Cache LRU theo token + dedupe trong cùng request (EZ_CT2_CACHE_LIMIT, 0 = tắt).
  - no_repeat_ngram_size / repetition_penalty chặn decode lặp vô hạn.
"""
import json
import os
import re
import sys
import time
from collections import OrderedDict
from pathlib import Path

import ctranslate2
import sentencepiece as spm

# ── CUDA DLL discovery ─────────────────────────────────────────────────────
# CTranslate2 wheel không kèm cuBLAS/cuDNN. Trên máy dev, PyTorch đã cài sẵn
# CUDA 12 runtime trong torch/lib; các bản pip khác có thể nằm ở nvidia/*/bin.
# Thêm các thư mục này trước khi tạo Translator để GPU dùng được mà không cần
# cài CUDA Toolkit toàn hệ thống.
def _add_dll_dir(path):
    path = str(path or "")
    if not path or not os.path.isdir(path):
        return
    try:
        os.add_dll_directory(path)
    except (AttributeError, OSError):
        pass
    os.environ["PATH"] = path + os.pathsep + os.environ.get("PATH", "")


def _prepare_cuda_dlls():
    if os.name != "nt":
        return
    try:
        import importlib.util

        spec = importlib.util.find_spec("torch")
        if spec and spec.origin:
            _add_dll_dir(Path(spec.origin).parent / "lib")
    except Exception:
        pass

    try:
        import site
        import sysconfig

        roots = set()
        try:
            roots.add(sysconfig.get_paths().get("purelib", ""))
        except Exception:
            pass
        try:
            roots.update(site.getsitepackages())
        except Exception:
            pass
        try:
            roots.add(site.getusersitepackages())
        except Exception:
            pass
        for root in roots:
            if not root:
                continue
            base = Path(root)
            for sub in ("nvidia/cublas/bin", "nvidia/cudnn/bin",
                        "nvidia/cuda_runtime/bin", "nvidia/cuda_nvrtc/bin"):
                _add_dll_dir(base / sub)
    except Exception:
        pass

    for env_name in ("CUDA_PATH", "CUDA_HOME"):
        root = os.environ.get(env_name)
        if root:
            _add_dll_dir(Path(root) / "bin")

# Marian mã hoá vị trí dừng ở 512 token; giữ biên an toàn và chặn luôn độ trễ.
MAX_SRC_TOKENS = 400

# beam 4: chất lượng tốt hơn hẳn greedy và rẻ (đo được trên máy này còn NHANH hơn
# beam 1 vì câu lặp vô hạn không còn bị đẩy tới hết max token). Chỉnh bằng env khi A/B.
BEAM = int(os.environ.get("EZ_CT2_BEAM", "2"))
# Pipeline thời gian thực ưu tiên phản hồi ổn định. 256 token cho một mảnh nguồn
# <=96 token khiến các output lỗi/lặp có thể giữ CPU nhiều giây; 128 vẫn đủ rộng
# cho EN->VI bình thường và chặn được đuôi decode bệnh lý.
MAX_DECODING = int(os.environ.get("EZ_CT2_MAX_TOKENS", "128"))

# ── Fast path ───────────────────────────────────────────────────────────────
FAST_MODE = os.environ.get("EZ_CT2_FAST", "1").strip().lower() not in ("0", "false", "no", "off")
FAST_MAX_TOKENS = int(os.environ.get("EZ_CT2_FAST_TOKENS", "96"))
FAST_BEAM = int(os.environ.get("EZ_CT2_FAST_BEAM", "1"))
FAST_MAX_BATCH = int(os.environ.get("EZ_CT2_FAST_BATCH", "8"))
FAST_NO_REPEAT = int(os.environ.get("EZ_CT2_NO_REPEAT", "6"))
FAST_REP_PENALTY = float(os.environ.get("EZ_CT2_REP_PENALTY", "1.05"))
CACHE_LIMIT = int(os.environ.get("EZ_CT2_CACHE_LIMIT", "2000"))
# Log chẩn đoán ra stderr (C++ ghi vào %TEMP%/ez_ct2.log). Tắt mặc định để
# không thêm I/O cho mỗi request; bật bằng EZ_CT2_LOG=1 khi cần đo.
LOG = os.environ.get("EZ_CT2_LOG", "0").strip().lower() not in ("0", "false", "no", "off")

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
        self.model_dir = model_dir
        self.intra_threads = max(1, intra_threads)
        self.requested_device = os.environ.get("EZ_CT2_DEVICE", "auto").strip().lower()
        self.requested_compute = os.environ.get("EZ_CT2_COMPUTE", "").strip()

        nllb_spm = os.path.join(model_dir, "sentencepiece.bpe.model")
        if os.path.exists(nllb_spm):
            self.kind = "nllb"
            self.sp_shared = load_spm(nllb_spm)
        else:
            self.kind = "marian"
            self.sp_src = load_spm(os.path.join(model_dir, "source.spm"))
            self.sp_tgt = load_spm(os.path.join(model_dir, "target.spm"))

        self.translator = None
        self.device = "cpu"
        self.compute_type = compute_type or "int8"
        self._load_translator()
        # GPU xử lý batch lớn tốt hơn nhiều: 20 câu thường ~70 ms thay vì
        # ~120-180 ms nếu chia batch 8. CPU giữ 8 để tránh padding.
        self.max_batch_size = int(os.environ.get(
            "EZ_CT2_FAST_BATCH", "32" if self.device == "cuda" else str(FAST_MAX_BATCH)))

        # Cache chỉ sống trong phiên worker (giống bản yuh). C++ vẫn có
        # TranslationCache riêng cho các lượt lặp giữa những lần gọi.
        self._cache = OrderedDict()

    def _make_translator(self, device, compute_type):
        return ctranslate2.Translator(
            self.model_dir,
            device=device,
            compute_type=compute_type,
            inter_threads=1,
            intra_threads=1 if device == "cuda" else self.intra_threads,
        )

    def _fallback_to_cpu(self):
        if self.device != "cuda":
            return False
        try:
            self.translator = self._make_translator("cpu", "int8")
            self.device = "cpu"
            self.compute_type = "int8"
            self.max_batch_size = FAST_MAX_BATCH
            print("[ct2] CUDA runtime failed -> fallback CPU int8",
                  file=sys.stderr, flush=True)
            return True
        except Exception as exc:
            print(f"[ct2] CPU fallback failed: {exc}", file=sys.stderr, flush=True)
            return False

    def _load_translator(self):
        want = self.requested_device
        if want not in ("cpu", "cuda", "gpu", "auto"):
            want = "auto"
        if want == "gpu":
            want = "cuda"

        if want in ("cuda", "auto"):
            _prepare_cuda_dlls()
            candidates = []
            if self.requested_compute:
                candidates.append(self.requested_compute)
            # float16 nhanh nhất trên RTX; int8_* là fallback cho GPU cũ.
            candidates.extend(["float16", "int8_float32", "float32"])
            last_error = None
            for compute_type in dict.fromkeys(candidates):
                try:
                    translator = self._make_translator("cuda", compute_type)
                    self.translator = translator
                    self.device = "cuda"
                    self.compute_type = compute_type
                    print(f"[ct2] device=cuda compute={compute_type}", file=sys.stderr, flush=True)
                    return
                except Exception as exc:
                    last_error = exc
            if want == "cuda":
                print(f"[ct2] CUDA không dùng được: {last_error}", file=sys.stderr, flush=True)

        # CPU fallback / explicit CPU.
        cpu_compute = self.requested_compute or "int8"
        try:
            self.translator = self._make_translator("cpu", cpu_compute)
            self.device = "cpu"
            self.compute_type = cpu_compute
        except Exception:
            self.translator = self._make_translator("cpu", "int8")
            self.device = "cpu"
            self.compute_type = "int8"
        print(f"[ct2] device=cpu compute={self.compute_type}", file=sys.stderr, flush=True)

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

    def _target_prefix(self, batch_size=1):
        if self.kind == "nllb" and self.tgt_lang:
            return [[self.tgt_lang]] * batch_size
        return None

    # ── Dịch ────────────────────────────────────────────────────────────────
    def _translate_tokens(self, token_lists, beam, no_repeat=0, rep_penalty=1.0,
                          max_batch_size=None):
        if not token_lists:
            return []
        kwargs = {
            "target_prefix": self._target_prefix(len(token_lists)),
            "beam_size": beam,
            "max_decoding_length": MAX_DECODING,
        }
        if max_batch_size:
            kwargs["max_batch_size"] = max_batch_size
        if no_repeat:
            kwargs["no_repeat_ngram_size"] = no_repeat
        if rep_penalty and rep_penalty != 1.0:
            kwargs["repetition_penalty"] = rep_penalty
        results = self.translator.translate_batch(token_lists, **kwargs)
        return [self._decode(r.hypotheses[0]) for r in results]

    def _translate_batch(self, texts):
        """Đường cũ: tokenize cả text, dịch một lượt (dùng cho quality mode)."""
        tokens = [self._encode(t) for t in texts]
        return self._translate_tokens(tokens, BEAM)

    # ── Fast path: câu + batch + cache ──────────────────────────────────────
    @staticmethod
    def _word_boundary(tokens, limit):
        limit = min(limit, len(tokens))
        if limit == len(tokens):
            return limit
        for position in range(limit, max(0, limit // 2), -1):
            if tokens[position].startswith("▁"):
                return position
        return limit

    def _chunk_sentence(self, tokens):
        chunks = []
        while len(tokens) > FAST_MAX_TOKENS:
            cut = self._word_boundary(tokens, FAST_MAX_TOKENS)
            chunks.append(tokens[:cut])
            tokens = tokens[cut:]
        if tokens:
            chunks.append(tokens)
        return chunks

    def _prepare(self, text):
        """Trả về (indices theo từng dòng, toàn bộ chunk token) cho một text."""
        lines = []
        inputs = []
        for line in text.split("\n"):
            indices = []
            pieces = [p for p in _SPLIT_RE.split(line) if p.strip()] or [line]
            for sentence in pieces:
                if not sentence.strip():
                    continue
                for chunk in self._chunk_sentence(self._encode(sentence.strip())):
                    indices.append(len(inputs))
                    inputs.append(chunk)
            lines.append(indices)
        return lines, inputs

    def _translate_cached(self, chunks):
        if not chunks:
            return []

        started = time.perf_counter()
        outputs = [""] * len(chunks)
        pending = {}
        cache_hits = 0

        for index, tokens in enumerate(chunks):
            key = tuple(tokens)
            if CACHE_LIMIT > 0 and key in self._cache:
                outputs[index] = self._cache[key]
                self._cache.move_to_end(key)
                cache_hits += 1
            else:
                # Câu trùng trong cùng request chỉ dịch một lần.
                pending.setdefault(key, []).append(index)

        if pending:
            keys = list(pending)
            try:
                fresh = self._translate_tokens(
                    [list(key) for key in keys],
                    FAST_BEAM,
                    no_repeat=FAST_NO_REPEAT,
                    rep_penalty=FAST_REP_PENALTY,
                    max_batch_size=self.max_batch_size,
                )
            except Exception as exc:
                print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)
                fresh = [""] * len(keys)
                if self._fallback_to_cpu():
                    try:
                        fresh = self._translate_tokens(
                            [list(key) for key in keys],
                            FAST_BEAM,
                            no_repeat=FAST_NO_REPEAT,
                            rep_penalty=FAST_REP_PENALTY,
                            max_batch_size=self.max_batch_size,
                        )
                    except Exception as retry_exc:
                        print(json.dumps({"error": str(retry_exc)}), file=sys.stderr,
                              flush=True)

            for key, output in zip(keys, fresh):
                # Câu rỗng: thử lại bằng cách chẻ đôi (giống worker của yuh).
                if not output and len(key) > 1:
                    output = self._translate_with_retry(list(key))

                for index in pending[key]:
                    outputs[index] = output

                # Không lưu bản dịch lỗi/rỗng vào cache.
                if CACHE_LIMIT > 0 and output and not output.startswith("[Chưa dịch:"):
                    self._cache[key] = output
                    self._cache.move_to_end(key)
                    while len(self._cache) > CACHE_LIMIT:
                        self._cache.popitem(last=False)

        duplicates = len(chunks) - cache_hits - len(pending)
        elapsed_ms = (time.perf_counter() - started) * 1000.0
        if LOG:
            print(
                f"[cache] hits={cache_hits} new={len(pending)} duplicates={duplicates} "
                f"stored={len(self._cache)} elapsed_ms={elapsed_ms:.1f}",
                file=sys.stderr,
                flush=True,
            )
        return outputs

    def _translate_with_retry(self, tokens, depth=0):
        if not tokens:
            return ""
        try:
            output = self._translate_tokens(
                [tokens], FAST_BEAM, no_repeat=FAST_NO_REPEAT,
                rep_penalty=FAST_REP_PENALTY,
            )[0]
        except Exception:
            output = ""
        if output or depth >= 3 or len(tokens) <= 1:
            return output or f"[Chưa dịch: {self._decode(tokens)}]"

        cut = self._word_boundary(tokens, len(tokens) // 2)
        if cut <= 0 or cut >= len(tokens):
            cut = len(tokens) // 2
        left = self._translate_with_retry(tokens[:cut], depth + 1)
        right = self._translate_with_retry(tokens[cut:], depth + 1)
        return " ".join(part for part in (left, right) if part)

    def _translate_fast(self, texts):
        """Batch toàn bộ câu của mọi text trong request vào một lượt."""
        started = time.perf_counter()
        structures = []
        inputs = []
        for text in texts:
            lines, chunks = self._prepare(text)
            # Chỉ số của _prepare là cục bộ trong từng text → cộng offset
            # để trỏ đúng vào batch chung của cả request.
            base = len(inputs)
            structures.append([[index + base for index in indices] for indices in lines])
            inputs.extend(chunks)

        translated = self._translate_cached(inputs)

        outputs = []
        for lines in structures:
            outputs.append(
                "\n".join(
                    " ".join(translated[i] for i in indices if translated[i])
                    for indices in lines
                )
            )

        if LOG:
            print(
                f"[fast] texts={len(texts)} chunks={len(inputs)} "
                f"elapsed_ms={(time.perf_counter() - started) * 1000.0:.1f}",
                file=sys.stderr,
                flush=True,
            )
        return outputs

    # ── API ─────────────────────────────────────────────────────────────────
    def translate(self, text):
        """Dịch một đơn vị; cắt câu quá dài thành nhiều mảnh để không bị cụt."""
        return self.translate_many([text])[0]

    def translate_many(self, texts):
        if FAST_MODE and self.kind == "marian":
            return self._translate_fast(texts)

        # Quality mode giữ nguyên hành vi cũ: gom tới 400 token, beam 4.
        outputs = []
        for text in texts:
            pieces = [p for p in _SPLIT_RE.split(text) if p.strip()] or [text]
            chunks, current = [], ""
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
                outputs.append(" ".join(self._translate_batch(chunks)).strip())
            except Exception as exc:  # một câu lỗi không được làm rỗng cả trang
                print(json.dumps({"error": str(exc)}), file=sys.stderr, flush=True)
                outputs.append("")
        return outputs


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

    # Warm-up bằng một batch nhỏ trước khi báo ready: GPU/CUDA kernel và
    # shape batch 8 được biên dịch sẵn, tránh 200-300 ms cho lượt dịch đầu.
    try:
        engine.translate_many([
            "Hello.", "Good morning.", "Open the door.", "Where is the key?",
            "Thank you very much.", "I am ready.", "See you tomorrow.",
            "The night is quiet.",
        ])
        print("[ct2] warmup done", file=sys.stderr, flush=True)
    except Exception as exc:
        print(f"[ct2] warmup skipped: {exc}", file=sys.stderr, flush=True)
        if engine._fallback_to_cpu():
            try:
                engine.translate_many([
                    "Hello.", "Good morning.", "Open the door.", "Where is the key?",
                    "Thank you very much.", "I am ready.", "See you tomorrow.",
                    "The night is quiet.",
                ])
            except Exception as retry_exc:
                print(f"[ct2] CPU warmup skipped: {retry_exc}", file=sys.stderr, flush=True)

    print(json.dumps({"ready": True, "kind": engine.kind,
                      "device": engine.device, "compute": engine.compute_type,
                      "mode": "fast" if (FAST_MODE and engine.kind == "marian") else "quality"}),
          flush=True)

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
        print(json.dumps({"translations": engine.translate_many(texts)}, ensure_ascii=False),
              flush=True)

    return 0


if __name__ == "__main__":
    sys.exit(main())
