#include <Python.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

void call_python_export(const char *format, const char *output_filename) {
    Py_Initialize();

    PyRun_SimpleString("import sys");
    PyRun_SimpleString("sys.path.append('python')");

    PyObject *pName = PyUnicode_FromString("export");
    PyObject *pModule = PyImport_Import(pName);
    Py_DECREF(pName);

    if (pModule) {
        PyObject *pFunc = PyObject_GetAttrString(pModule, "export_data");
        if (pFunc && PyCallable_Check(pFunc)) {
            PyObject *args = PyTuple_Pack(3,
                PyUnicode_FromString("tmp_export.csv"),
            PyUnicode_FromString(format),
            PyUnicode_FromString(output_filename)
            );

            PyObject *pResult = PyObject_CallObject(pFunc, args);
            Py_DECREF(args);

            if (!pResult) {
                PyErr_Print();
            } else {
                Py_DECREF(pResult);
            }

            Py_XDECREF(pFunc);
        } else {
            PyErr_Print();
        }
        Py_DECREF(pModule);
    } else {
        PyErr_Print();
    }

    Py_Finalize();
}

bool is_python_available(void) {
    int ret = system("python3 --version > /dev/null 2>&1");
    return (ret == 0);
}