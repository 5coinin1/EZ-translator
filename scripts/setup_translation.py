import hashlib
import json
from pathlib import Path
import shutil
import sys
import tempfile
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEST = ROOT / "models" / "translation" / "en_vi"

URL = (
    "https://data.argosopentech.com/argospm/v1/"
    "translate-en_vi-1_9.argosmodel"
)

SHA256 = (
    "86957101aa4099aa9a1a7492e41987d938"
    "d3cf0fdaf4fb684c0797a9d567dd16"
)

PACKAGE_ROOT = "translate-en_vi-1_9"

FILES = (
    "metadata.json",
    "README.md",
    "sentencepiece.model",
    "model/config.json",
    "model/model.bin",
    "model/shared_vocabulary.json",
)


def check_model(directory):
    for name in FILES:
        if not (directory / name).is_file():
            raise RuntimeError(f"Thieu file: {directory / name}")

    metadata = json.loads(
        (directory / "metadata.json").read_text(encoding="utf-8")
    )

    languages = (
        metadata.get("from_code"),
        metadata.get("to_code"),
    )

    if languages != ("en", "vi"):
        raise RuntimeError("Mo hinh khong phai Anh -> Viet.")


def main():
    if DEST.exists():
        check_model(DEST)
        print(f"Da co mo hinh: {DEST}")
        return

    DEST.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(dir=DEST.parent) as temporary:
        temporary = Path(temporary)
        archive = temporary / "model.argosmodel"

        print(
            "Dang tai mo hinh Anh -> Viet, khoang 68 MB...",
            flush=True,
        )

        digest = hashlib.sha256()

        with urllib.request.urlopen(URL, timeout=60) as response:
            with archive.open("wb") as output:
                while True:
                    chunk = response.read(1024 * 1024)
                    if not chunk:
                        break

                    digest.update(chunk)
                    output.write(chunk)

        if digest.hexdigest() != SHA256:
            raise RuntimeError(
                "SHA256 khong khop. Khong cai dat file vua tai."
            )

        staged = temporary / "en_vi"

        with zipfile.ZipFile(archive) as package:
            for name in FILES:
                destination = staged / name
                destination.parent.mkdir(parents=True, exist_ok=True)

                with package.open(f"{PACKAGE_ROOT}/{name}") as source:
                    with destination.open("wb") as output:
                        shutil.copyfileobj(source, output)

        check_model(staged)
        staged.rename(DEST)

    print(f"Da cai mo hinh: {DEST}")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"LOI: {error}", file=sys.stderr)
        raise SystemExit(1)