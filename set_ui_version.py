import sys
import os

def set_ui_version(version):
    index_html_path = os.path.join("data", "index.html")
    with open(index_html_path, "r") as f:
        content = f.read()

    content = content.replace("UI_VERSION_PLACEHOLDER", version)

    with open(index_html_path, "w") as f:
        f.write(content)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python set_ui_version.py <version>")
        sys.exit(1)

    version = sys.argv[1]
    set_ui_version(version)
