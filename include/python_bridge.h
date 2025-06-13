#ifndef PYTHON_BRIDGE_H
#define PYTHON_BRIDGE_H

void call_python_export(const char *format, const char *output_filename);

bool is_python_available(void);

#endif // PYTHON_BRIDGE_H