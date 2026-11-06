Unmodified reference snapshot from Ubuntu Unity:
https://gitlab.com/ubuntu-unity/unity/unity

Commit: 2fb9954c7c17736d3f68e0d3ae26683790f812bf

`launcher/` contains all 126 original launcher files. `unity-shared/` contains
renderer references; `resources/` contains original artwork used by the port.
The production executable does not compile or link the C++ reference files.

`manifest.json` records upstream Git blob IDs for the full launcher directory
and additional files acquired through the GitLab repository API. Verify with:

    python3 tools/verify-upstream.py

Original copyright/license notices are retained. See `COPYING` for GPLv3.
