// Unless explicitly stated otherwise all files in this repository are licensed
// under the Apache License Version 2.0.
// This product includes software developed at Datadog (https://www.datadoghq.com/).
// Copyright 2019 Datadog, Inc.

#include "three.h"
#include "constants.h"

#include <sstream>

PyModuleConstants Three::ModuleConstants;
std::mutex Three::ModuleConstantsMtx;

// we only populate the fields `m_base` and `m_name`, we don't need any of the
// rest since we're doing Single-phase initialization
//
// INIT_PYTHON_MODULE creates the def_<moduleName> (a PyModuleDef struct) and
// the needed PyInit_<moduleName> callback.
#define INIT_PYTHON_MODULE(moduleID, moduleName)                                                                       \
    static struct PyModuleDef def_##moduleName                                                                         \
        = { PyModuleDef_HEAD_INIT, datadog_agent_six_##moduleName, NULL, -1, NULL, NULL, NULL, NULL, NULL };           \
    PyMODINIT_FUNC PyInit_##moduleName(void) {                                                                         \
        PyObject *m = PyModule_Create(&def_##moduleName);                                                              \
        std::lock_guard<std::mutex> lock(Three::ModuleConstantsMtx);                                                   \
        PyModuleConstants::iterator it = Three::ModuleConstants.find(moduleID);                                        \
        if (it != Three::ModuleConstants.end()) {                                                                      \
            std::vector<PyModuleConst>::iterator cit;                                                                  \
            for (cit = it->second.begin(); cit != it->second.end(); ++cit) {                                           \
                PyModule_AddIntConstant(m, cit->first.c_str(), cit->second);                                           \
            }                                                                                                          \
        }                                                                                                              \
        return m;                                                                                                      \
    }

// APPEND_TO_PYTHON_INITTAB set the module methods and call
// PyImport_AppendInittab with it, allowing Python to import it
#define APPEND_TO_PYTHON_INITTAB(moduleID, moduleName)                                                                 \
    {                                                                                                                  \
        if (_modules[moduleID].size() > 0) {                                                                           \
            def_##moduleName.m_methods = &_modules[moduleID][0];                                                       \
            if (PyImport_AppendInittab(getExtensionModuleName(moduleID), &PyInit_##moduleName) == -1) {                \
                setError("PyImport_AppendInittab failed to append " #moduleName);                                      \
                return 1;                                                                                              \
            }                                                                                                          \
        }                                                                                                              \
    }

// initializing all Python C module
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_DATADOG_AGENT, datadog_agent)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX__UTIL, _util)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_UTIL, util)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_AGGREGATOR, aggregator)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_CONTAINERS, containers)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_KUBEUTIL, kubeutil)
INIT_PYTHON_MODULE(DATADOG_AGENT_SIX_TAGGER, tagger)

Three::~Three() {
    if (_pythonHome) {
        PyMem_RawFree((void *)_pythonHome);
    }
    Py_Finalize();
    ModuleConstants.clear();
}

int Three::init(const char *pythonHome) {
    // insert module to Python inittab one by one
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_DATADOG_AGENT, datadog_agent)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX__UTIL, _util)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_UTIL, util)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_AGGREGATOR, aggregator)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_CONTAINERS, containers)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_KUBEUTIL, kubeutil)
    APPEND_TO_PYTHON_INITTAB(DATADOG_AGENT_SIX_TAGGER, tagger)

    // We need to initialize python before we can call Py_DecodeLocale
    Py_Initialize();
    if (pythonHome == NULL) {
        _pythonHome = Py_DecodeLocale(_defaultPythonHome, NULL);
    } else {
        if (_pythonHome) {
            PyMem_RawFree((void *)_pythonHome);
        }
        _pythonHome = Py_DecodeLocale(pythonHome, NULL);
    }

    Py_SetPythonHome(_pythonHome);
    return 0;
}

bool Three::isInitialized() const { return Py_IsInitialized(); }
const char *Three::getPyVersion() const { return Py_GetVersion(); }
int Three::runSimpleString(const char *code) const { return PyRun_SimpleString(code); }

int Three::addModuleFunction(six_module_t module, six_module_func_t t, const char *funcName, void *func) {
    if (getExtensionModuleName(module) == getUnknownModuleName()) {
        setError("Unknown ExtensionModule value");
        return -1;
    }

    int ml_flags;
    switch (t) {
    case DATADOG_AGENT_SIX_NOARGS:
        ml_flags = METH_NOARGS;
        break;
    case DATADOG_AGENT_SIX_ARGS:
        ml_flags = METH_VARARGS;
        break;
    case DATADOG_AGENT_SIX_KEYWORDS:
        ml_flags = METH_VARARGS | METH_KEYWORDS;
        break;
    default:
        setError("Unknown MethType value");
        return -1;
    }

    PyMethodDef def = { funcName, (PyCFunction)func, ml_flags, "" };

    if (_modules.find(module) == _modules.end()) {
        _modules[module] = PyMethods();
        // add the guard
        PyMethodDef guard = { NULL, NULL, 0, NULL };
        _modules[module].push_back(guard);
    }

    // insert at beginning so we keep guard at the end
    _modules[module].insert(_modules[module].begin(), def);

    return 0;
}

int Three::addModuleIntConst(six_module_t moduleID, const char *name, long value) {
    std::lock_guard<std::mutex> lock(Three::ModuleConstantsMtx);
    if (ModuleConstants.find(moduleID) == ModuleConstants.end()) {
        ModuleConstants[moduleID] = std::vector<PyModuleConst>();
    }

    ModuleConstants[moduleID].push_back(std::make_pair(std::string(name), value));
    return 0;
}

six_gilstate_t Three::GILEnsure() {
    PyGILState_STATE state = PyGILState_Ensure();
    if (state == PyGILState_LOCKED) {
        return DATADOG_AGENT_SIX_GIL_LOCKED;
    }
    return DATADOG_AGENT_SIX_GIL_UNLOCKED;
}

void Three::GILRelease(six_gilstate_t state) {
    if (state == DATADOG_AGENT_SIX_GIL_LOCKED) {
        PyGILState_Release(PyGILState_LOCKED);
    } else {
        PyGILState_Release(PyGILState_UNLOCKED);
    }
}

SixPyObject *Three::getCheckClass(const char *module) {
    PyObject *base = NULL;
    PyObject *obj_module = NULL;
    PyObject *klass = NULL;

    base = _importFrom("datadog_checks.base.checks", "AgentCheck");
    if (base == NULL) {
        std::string old_err = getError();
        setError("Unable to import the base class: " + old_err);
        goto done;
    }

    obj_module = PyImport_ImportModule(module);
    if (obj_module == NULL) {
        PyErr_Print();
        std::ostringstream err;
        err << "unable to import module '" << module << "': " + _fetchPythonError();
        setError(err.str());
        goto done;
    }

    // find a subclass of the base check
    klass = _findSubclassOf(base, obj_module);
    if (klass == NULL) {
        std::ostringstream err;
        err << "unable to find a subclass of the base check in module '" << module << "': " << _fetchPythonError();
        setError(err.str());
        goto done;
    }

done:
    Py_XDECREF(base);
    Py_XDECREF(obj_module);
    Py_XDECREF(klass);

    if (klass == NULL) {
        return NULL;
    }

    return reinterpret_cast<SixPyObject *>(klass);
}

SixPyObject *Three::getCheck(const char *module, const char *init_config_str, const char *instances_str) {
    PyObject *klass = NULL;
    PyObject *init_config = NULL;
    PyObject *instances = NULL;
    PyObject *check = NULL;
    PyObject *args = NULL;
    PyObject *kwargs = NULL;

    char load_config[] = "load_config";
    char format[] = "(s)";

    // Gets Check class from module
    klass = reinterpret_cast<PyObject *>(getCheckClass(module));
    if (klass == NULL) {
        // Error is already set by getCheckClass if class is not found
        goto done;
    }

    // call `AgentCheck.load_config(init_config)`
    init_config = PyObject_CallMethod(klass, load_config, format, init_config_str);
    if (init_config == NULL) {
        PyErr_Print();
        goto done;
    }

    // call `AgentCheck.load_config(instances)`
    instances = PyObject_CallMethod(klass, load_config, format, instances_str);
    if (instances == NULL) {
        PyErr_Print();
        goto done;
    }

    // create `args` and `kwargs` to invoke `AgentCheck` constructor
    args = PyTuple_New(0);
    kwargs = PyDict_New();
    PyDict_SetItemString(kwargs, "init_config", init_config);
    PyDict_SetItemString(kwargs, "instances", instances);

    // call `AgentCheck` constructor
    check = PyObject_Call(klass, args, kwargs);
    if (check == NULL) {
        PyErr_Print();
        goto done;
    }

done:
    Py_XDECREF(klass);
    Py_XDECREF(init_config);
    Py_XDECREF(instances);
    Py_XDECREF(args);
    Py_XDECREF(kwargs);

    if (check == NULL) {
        return NULL;
    }

    return reinterpret_cast<SixPyObject *>(check);
}

const char *Three::runCheck(SixPyObject *check) {
    if (check == NULL) {
        return NULL;
    }

    PyObject *py_check = reinterpret_cast<PyObject *>(check);

    // result will be eventually returned as a copy and the corresponding Python
    // string decref'ed, caller will be responsible for memory deallocation.
    char *ret, *ret_copy = NULL;
    char run[] = "run";
    PyObject *result, *bytes = NULL;

    result = PyObject_CallMethod(py_check, run, NULL);
    if (result == NULL || !PyUnicode_Check(result)) {
        PyErr_Print();
        goto done;
    }

    bytes = PyUnicode_AsEncodedString(result, "UTF-8", "strict");
    if (bytes == NULL) {
        PyErr_Print();
        goto done;
    }

    // `ret` points to the Python string internal storage and will be eventually
    // deallocated along with the corresponding Python object.
    ret = PyBytes_AsString(bytes);
    ret_copy = strdup(ret);
    Py_XDECREF(bytes);

done:
    Py_XDECREF(result);
    return ret_copy;
}

// return new reference
PyObject *Three::_importFrom(const char *module, const char *name) {
    PyObject *obj_module, *obj_symbol;

    obj_module = PyImport_ImportModule(module);
    if (obj_module == NULL) {
        setError(_fetchPythonError());
        goto error;
    }

    obj_symbol = PyObject_GetAttrString(obj_module, name);
    if (obj_symbol == NULL) {
        setError(_fetchPythonError());
        goto error;
    }

    return obj_symbol;

error:
    Py_XDECREF(obj_module);
    Py_XDECREF(obj_symbol);
    return NULL;
}

PyObject *Three::_findSubclassOf(PyObject *base, PyObject *module) {
    if (base == NULL || !PyType_Check(base)) {
        setError("base class is not of type 'Class'");
        return NULL;
    }

    if (module == NULL || !PyModule_Check(module)) {
        setError("module is not of type 'Module'");
        return NULL;
    }

    PyObject *dir = PyObject_Dir(module);
    if (dir == NULL) {
        setError("there was an error calling dir() on module object");
        return NULL;
    }

    PyObject *klass = NULL;
    for (int i = 0; i < PyList_GET_SIZE(dir); i++) {
        // get symbol name
        char *symbol_name;
        PyObject *symbol = PyList_GetItem(dir, i);
        if (symbol != NULL) {
            PyObject *bytes = PyUnicode_AsEncodedString(symbol, "UTF-8", "strict");

            if (bytes != NULL) {
                symbol_name = strdup(PyBytes_AsString(bytes));
                Py_XDECREF(bytes);
            } else {
                continue;
            }
        } else {
            // Gets exception reason
            PyObject *reason = PyUnicodeDecodeError_GetReason(PyExc_IndexError);

            // Clears exception and sets error
            PyException_SetTraceback(PyExc_IndexError, Py_None);
            setError(strdup(PyBytes_AsString(reason)));
            goto done;
        }

        klass = PyObject_GetAttrString(module, symbol_name);
        if (klass == NULL) {
            continue;
        }

        // Not a class, ignore
        if (!PyType_Check(klass)) {
            Py_XDECREF(klass);
            continue;
        }

        // Unrelated class, ignore
        if (!PyType_IsSubtype((PyTypeObject *)klass, (PyTypeObject *)base)) {
            Py_XDECREF(klass);
            continue;
        }

        // `klass` is actually `base` itself, ignore
        if (PyObject_RichCompareBool(klass, base, Py_EQ) == 1) {
            Py_XDECREF(klass);
            continue;
        }

        // does `klass` have subclasses?
        char func_name[] = "__subclasses__";
        PyObject *children = PyObject_CallMethod(klass, func_name, NULL);
        if (children == NULL) {
            Py_XDECREF(klass);
            continue;
        }

        // how many?
        int children_count = PyList_GET_SIZE(children);
        Py_XDECREF(children);

        // Agent integrations are supposed to have no subclasses
        if (children_count > 0) {
            Py_XDECREF(klass);
            continue;
        }

        // got it, return the check class
        goto done;
    }

    setError("cannot find a subclass");

done:
    Py_DECREF(dir);
    return klass;
}

std::string Three::_fetchPythonError() {
    std::string ret_val = "";

    if (PyErr_Occurred() == NULL) {
        return ret_val;
    }

    PyObject *ptype = NULL;
    PyObject *pvalue = NULL;
    PyObject *ptraceback = NULL;

    // Fetch error and make sure exception values are normalized, as per python C API docs.
    PyErr_Fetch(&ptype, &pvalue, &ptraceback);
    PyErr_NormalizeException(&ptype, &pvalue, &ptraceback);

    // There's a traceback, try to format it nicely
    if (ptraceback != NULL) {
        PyObject *traceback = PyImport_ImportModule("traceback");
        if (traceback != NULL) {
            char fname[] = "format_exception";
            PyObject *format_exception = PyObject_GetAttrString(traceback, fname);
            if (format_exception != NULL) {
                PyObject *fmt_exc = PyObject_CallFunctionObjArgs(format_exception, ptype, pvalue, ptraceback, NULL);
                if (fmt_exc != NULL) {
                    // "format_exception" returns a list of strings (one per line)
                    for (int i = 0; i < PyList_Size(fmt_exc); i++) {
                        PyObject *temp_bytes = PyUnicode_AsEncodedString(PyList_GetItem(fmt_exc, i), "UTF-8", "strict");
                        ret_val += PyBytes_AS_STRING(temp_bytes);
                        Py_XDECREF(temp_bytes);
                    }
                }
                Py_XDECREF(fmt_exc);
                Py_XDECREF(format_exception);
            }
            Py_XDECREF(traceback);
        } else {
            // If we reach this point, there was an error while formatting the exception
            ret_val = "can't format exception";
        }
    }
    // we sometimes do not get a traceback but an error in pvalue
    else if (pvalue != NULL) {
        PyObject *pvalue_obj = PyObject_Str(pvalue);
        if (pvalue_obj != NULL) {
            PyObject *temp_bytes = PyUnicode_AsEncodedString(pvalue_obj, "UTF-8", "strict");
            ret_val = PyBytes_AS_STRING(temp_bytes);
            Py_XDECREF(pvalue_obj);
            Py_XDECREF(temp_bytes);
        }
    } else if (ptype != NULL) {
        PyObject *ptype_obj = PyObject_Str(ptype);
        if (ptype_obj != NULL) {
            PyObject *temp_bytes = PyUnicode_AsEncodedString(ptype_obj, "UTF-8", "strict");
            ret_val = PyBytes_AS_STRING(temp_bytes);
            Py_XDECREF(ptype_obj);
            Py_XDECREF(temp_bytes);
        }
    }

    if (ret_val == "") {
        ret_val = "unknown error";
    }

    // clean up and return the string
    PyErr_Clear();
    Py_XDECREF(ptype);
    Py_XDECREF(pvalue);
    Py_XDECREF(ptraceback);
    return ret_val;
}