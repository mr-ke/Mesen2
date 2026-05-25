#!/usr/bin/env python3
import zipfile
import os
import sys

zip_file = sys.argv[1]
source_dir = sys.argv[2] if len(sys.argv) > 2 else '.'

with zipfile.ZipFile(zip_file, 'w', zipfile.ZIP_DEFLATED) as z:
    for root, dirs, files in os.walk(source_dir):
        for f in files:
            file_path = os.path.join(root, f)
            arcname = os.path.relpath(file_path, source_dir)
            z.write(file_path, arcname)
