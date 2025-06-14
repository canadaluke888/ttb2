from setuptools import setup, Extension

module = Extension(
    "tablecraft",
    sources=[
        "src/py_tablecraft_module.c",
        "src/table.c",
        "src/errors.c",
        "src/python_bridge.c",
        "src/ui_draw.c",
        "src/ui_edit.c",
        "src/ui_init.c",
        "src/ui_loop.c",
        "src/ui_prompt.c",  # include prompt_add_row and related prompts
    ],
    include_dirs=["include"],
    libraries=["ncursesw"],
)

setup(
    name="tablecraft",
    version="0.1.1",
    description="Terminal-based table editor (ncurses UI)",
    ext_modules=[module],
)