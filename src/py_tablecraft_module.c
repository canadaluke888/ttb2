#include <Python.h>
#include "tablecraft.h"

// Python wrapper for running the TableCraft UI.
// Usage: tablecraft.run([title])
static PyObject *py_run(PyObject *self, PyObject *args) {
    const char *title = NULL;
    if (!PyArg_ParseTuple(args, "|s", &title)) {
        return NULL;
    }
    if (!title) {
        title = "Untitled Table";
    }
    Table *table = create_table(title);
    start_ui_loop(table);
    free_table(table);
    Py_RETURN_NONE;
}

static PyMethodDef TableCraftMethods[] = {
    {"run", py_run, METH_VARARGS, "Run the TableCraft UI (optional table title)"},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef tablecraftmodule = {
    PyModuleDef_HEAD_INIT,
    "tablecraft",
    "Terminal-based table editor module",
    -1,
    TableCraftMethods
};

PyMODINIT_FUNC PyInit_tablecraft(void) {
    return PyModule_Create(&tablecraftmodule);
}