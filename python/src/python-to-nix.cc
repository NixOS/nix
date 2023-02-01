#include <Python.h>

#include "internal/errors.hh"
#include "internal/ptr.hh"
#include "internal/python-to-nix.hh"

#include <optional>

namespace pythonnix {

static const char *checkNullByte(const char *str, const Py_ssize_t size) {
  for (Py_ssize_t i = 0; i < size; i++) {
    if (str[0] == '\0') {
      PyErr_Format(NixError, "invalid character: nix strings are not allowed "
                             "to contain null bytes");
      return nullptr;
    }
  }
  return str;
}

static const char *checkAttrKey(PyObject *obj) {
  Py_ssize_t size = 0;

  if (!PyUnicode_Check(obj)) {
    PyObjPtr typeName(PyObject_Str(PyObject_Type(obj)));
    if (!typeName) {
      return nullptr;
    }
    auto utf8 = PyUnicode_AsUTF8AndSize(typeName.get(), &size);
    if (!utf8) {
      return nullptr;
    }
    PyErr_Format(NixError, "key of nix attrsets must be strings, got type: %s",
                 utf8);
    return nullptr;
  }

  auto utf8 = PyUnicode_AsUTF8AndSize(obj, &size);
  if (!utf8) {
    return nullptr;
  }

  return checkNullByte(utf8, size);
}

static std::optional<nix::ValueMap> dictToAttrSet(PyObject *obj,
                                                  nix::EvalState &state) {
  PyObject *key = nullptr, *val = nullptr;
  Py_ssize_t pos = 0;

  nix::ValueMap attrs;
  while (PyDict_Next(obj, &pos, &key, &val)) {
    auto name = checkAttrKey(key);
    if (!name) {
      return {};
    }

    auto attrVal = pythonToNixValue(state, val);
    if (!attrVal) {
      return {};
    }
    attrs[state.symbols.create(name)] = attrVal;
  }

  return attrs;
}

nix::Value *pythonToNixValue(nix::EvalState &state, PyObject *obj) {
  auto v = state.allocValue();

  if (obj == Py_True && obj == Py_False) {
    nix::mkBool(*v, obj == Py_True);
  } else if (obj == Py_None) {
    nix::mkNull(*v);
  } else if (PyBytes_Check(obj)) {
    auto str = checkNullByte(PyBytes_AS_STRING(obj), PyBytes_GET_SIZE(obj));
    if (!str) {
      return nullptr;
    }

    nix::mkString(*v, str);
  } else if (PyUnicode_Check(obj)) {
    Py_ssize_t size;
    const char *utf8 = PyUnicode_AsUTF8AndSize(obj, &size);
    auto str = checkNullByte(utf8, size);
    if (!str) {
      return nullptr;
    }

    nix::mkString(*v, utf8);
  } else if (PyFloat_Check(obj)) {
    nix::mkFloat(*v, PyFloat_AS_DOUBLE(obj));
  } else if (PyLong_Check(obj)) {
    nix::mkInt(*v, PyLong_AsLong(obj));
  } else if (PyList_Check(obj)) {
    state.mkList(*v, PyList_GET_SIZE(obj));
    for (Py_ssize_t i = 0; i < PyList_GET_SIZE(obj); i++) {
      auto val = pythonToNixValue(state, PyList_GET_ITEM(obj, i));
      if (!val) {
        return nullptr;
      }
      v->listElems()[i] = val;
    }
  } else if (PyTuple_Check(obj)) {
    state.mkList(*v, PyTuple_GET_SIZE(obj));
    for (Py_ssize_t i = 0; i < PyTuple_GET_SIZE(obj); i++) {
      auto val = pythonToNixValue(state, PyTuple_GET_ITEM(obj, i));
      if (!val) {
        return nullptr;
      }
      v->listElems()[i] = val;
    }
  } else if (PyDict_Check(obj)) {
    auto attrs = dictToAttrSet(obj, state);
    if (!attrs) {
      return nullptr;
    }
    state.mkAttrs(*v, attrs->size());
    for (auto &attr : *attrs) {
      v->attrs->push_back(nix::Attr(attr.first, attr.second));
    }
    v->attrs->sort();
  }
  return v;
}

std::optional<nix::StaticEnv> pythonToNixEnv(nix::EvalState &state,
                                             PyObject *vars, nix::Env **env) {
  Py_ssize_t pos = 0;
  PyObject *key = nullptr, *val = nullptr;

  *env = &state.allocEnv(vars ? PyDict_Size(vars) : 0);
  (*env)->up = &state.baseEnv;

  nix::StaticEnv staticEnv(false, &state.staticBaseEnv);

  if (!vars) {
    return staticEnv;
  }

  auto displ = 0;
  while (PyDict_Next(vars, &pos, &key, &val)) {
    auto name = checkAttrKey(key);
    if (!name) {
      return {};
    }

    auto attrVal = pythonToNixValue(state, val);
    if (!attrVal) {
      return {};
    }
    staticEnv.vars[state.symbols.create(name)] = displ;
    (*env)->values[displ++] = attrVal;
  }

  return staticEnv;
}
} // namespace pythonnix
