#include <node_api.h>
#include <string.h>
#include <stdio.h>

#include "common.h"

struct InterfaceCallbackContext {
  napi_threadsafe_function tsfn = nullptr;
  napi_ref user_data_ref = nullptr;

  void release(napi_env env) {
    if (tsfn)
      napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
    if (user_data_ref)
      napi_delete_reference(env, user_data_ref);
  }
};

static std::unordered_map<PWAOS_OBJECT_INTERFACE_HANDLE, napi_ref> g_interfaces;

static void interface_obj_finalizer(napi_env env,
                                    void* finalize_data,
                                    void* finalize_hint) {
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle = (PWAOS_OBJECT_INTERFACE_HANDLE)finalize_data;
  if (!interface_handle)
    return;
  pwaosInstance_destroyObjectInterface(interface_handle);
  const auto& it = g_interfaces.find(interface_handle);
  if (it != g_interfaces.end()) {
    napi_delete_reference(env, it->second);
    g_interfaces.erase(it);
  }
}

static bool ObjectInterfaceMethodCallback(
  int argc,
  const char* argvJsonStr,
  char* resultJsonStr,
  int maxResultJsonStrLength,
  void* userData) {
  auto* callback_ctx = (InterfaceCallbackContext*)userData;
  return false;
}
static void ObjectInterfaceMethodCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  napi_value recv;
  if (napi_get_undefined(env, &recv) != napi_ok) {
    napi_throw_error(env,
                     "ERR_NAPI_TSFN_GET_UNDEFINED",
                     "Failed to retrieve undefined value");
    return;
  }

  // 如果有 userData，还需要取出
  napi_value user_data;
  auto* ctx = (InterfaceCallbackContext*)context;
  napi_value args[1];
  size_t argc = 0;
  if (napi_ok == napi_get_reference_value(env, ctx->user_data_ref, &user_data)) {
    args[0] = user_data;
    argc = 1;
  }
  napi_call_function(env, recv, js_cb, argc, args, nullptr);
}
static napi_value ObjectInterfaceDefineMethod(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 3) {
    napi_throw_type_error(env, nullptr, "Expected 3 arguments: name, methodCallback, userData");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle;
  napi_unwrap(env, this_arg, (void**)&interface_handle);
  if (!interface_handle) {
    napi_throw_error(env, nullptr, "Invalid interface handle");
    return nullptr;
  }

  // 获取方法名
  napi_valuetype name_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &name_type));
  if (name_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }
  size_t name_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &name_len));
  char name[256];
  if (name_len >= sizeof(name)) {
    napi_throw_error(env, nullptr, "Method name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  napi_valuetype callback_type;
  NODE_API_CALL(env, napi_typeof(env, argv[1], &callback_type));
  if (callback_type != napi_function) {
    napi_throw_type_error(env, nullptr, "Third argument must be an function");
    return nullptr;
  }

  auto interface_method_ctx = std::make_unique<InterfaceCallbackContext>();
  if (!interface_method_ctx) {
    napi_throw_error(env, nullptr, "Out of memory!");
    return nullptr;
  }
  auto* raw_interface_method_ctx = interface_method_ctx.get();

  napi_value async_name;
  NODE_API_CALL(env, napi_create_string_utf8(env, "ObjectInterfaceMethodCallback", NAPI_AUTO_LENGTH, &async_name));
  NODE_API_CALL(env, napi_create_threadsafe_function(env, argv[1], nullptr, async_name, 0, 1, nullptr,
                                  nullptr, raw_interface_method_ctx, ObjectInterfaceMethodCallback, &raw_interface_method_ctx->tsfn));

  napi_create_reference(env, argv[2], 1, &raw_interface_method_ctx->user_data_ref);

  interface_method_ctx.release();
  PWAOS_API_CALL_AND_RETURN(env,
                            pwaosObjectInterface_defineMethod(interface_handle, name, ObjectInterfaceMethodCallback, raw_interface_method_ctx));
}

static napi_value ObjectInterfaceRemoveMethod(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: name");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle;
  napi_unwrap(env, this_arg, (void**)&interface_handle);
  if (!interface_handle) {
    napi_throw_error(env, nullptr, "Invalid interface handle");
    return nullptr;
  }

  // 获取方法名
  napi_valuetype name_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &name_type));
  if (name_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }
  size_t name_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &name_len));
  char name[256];
  if (name_len >= sizeof(name)) {
    napi_throw_error(env, nullptr, "Method name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  PWAOS_API_CALL_AND_RETURN(env, pwaosObjectInterface_removeMethod(interface_handle, name));
}

struct PropertyCallbackContext {
  napi_threadsafe_function tsfn_getter = nullptr;
  napi_threadsafe_function tsfn_setter = nullptr;
  napi_ref user_data_ref = nullptr;

  void release(napi_env env) {
    if (tsfn_getter)
      napi_release_threadsafe_function(tsfn_getter, napi_tsfn_abort);
    if (tsfn_setter)
      napi_release_threadsafe_function(tsfn_setter, napi_tsfn_abort);
    if (user_data_ref)
      napi_delete_reference(env, user_data_ref);
  }
};
static bool ObjectInterfaceSetPropertyCallback(
                                      const char* propJsonStr,
                                      void* userData) {
  return false;
}
static bool ObjectInterfaceGetPropertyCallback(
                                      char* out_propJsonStr,
                                      int maxPropJsonStrLength,
                                      void* userData) {
  return false;
}
static void ObjectInterfacePropertyCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  napi_value recv;
  if (napi_get_undefined(env, &recv) != napi_ok) {
    napi_throw_error(env,
                     "ERR_NAPI_TSFN_GET_UNDEFINED",
                     "Failed to retrieve undefined value");
    return;
  }

  // 如果有 userData，还需要取出
  napi_value user_data;
  auto* ctx = (InterfaceCallbackContext*)context;
  napi_value args[1];
  size_t argc = 0;
  if (napi_ok == napi_get_reference_value(env, ctx->user_data_ref, &user_data)) {
    args[0] = user_data;
    argc = 1;
  }
  napi_call_function(env, recv, js_cb, argc, args, nullptr);
}
static napi_value ObjectInterfaceDefineProperty(napi_env env, napi_callback_info info) {
  size_t argc = 4;
  napi_value argv[4];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 4) {
    napi_throw_type_error(env, nullptr, "Expected 4 arguments: name, setterCallback, getterCallback, userData");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle;
  napi_unwrap(env, this_arg, (void**)&interface_handle);
  if (!interface_handle) {
    napi_throw_error(env, nullptr, "Invalid interface handle");
    return nullptr;
  }

  // 获取属性名
  napi_valuetype name_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &name_type));
  if (name_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }
  size_t name_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &name_len));
  char name[256];
  if (name_len >= sizeof(name)) {
    napi_throw_error(env, nullptr, "Property name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  napi_valuetype callback_type;
  NODE_API_CALL(env, napi_typeof(env, argv[1], &callback_type));
  if (callback_type != napi_function) {
    napi_throw_type_error(env, nullptr, "Second argument must be an function");
    return nullptr;
  }

  NODE_API_CALL(env, napi_typeof(env, argv[2], &callback_type));
  if (callback_type != napi_function) {
    napi_throw_type_error(env, nullptr, "Third argument must be an function");
    return nullptr;
  }

  auto interface_property_ctx = std::make_unique<PropertyCallbackContext>();
  if (!interface_property_ctx) {
    napi_throw_error(env, nullptr, "Out of memory!");
    return nullptr;
  }
  auto* raw_interface_property_ctx = interface_property_ctx.get();

  napi_value async_name_getter;
  NODE_API_CALL(env, napi_create_string_utf8(env, "ObjectInterfacePropertyGetterCallback", NAPI_AUTO_LENGTH, &async_name_getter));
  NODE_API_CALL(env, napi_create_threadsafe_function(env, argv[1], nullptr, async_name_getter, 0, 1, nullptr,
                                  nullptr, raw_interface_property_ctx, ObjectInterfacePropertyCallback, &raw_interface_property_ctx->tsfn_getter));

  napi_value async_name_setter;
  NODE_API_CALL(env, napi_create_string_utf8(env, "ObjectInterfacePropertySetterCallback", NAPI_AUTO_LENGTH, &async_name_setter));
  NODE_API_CALL(env, napi_create_threadsafe_function(env, argv[2], nullptr, async_name_setter, 0, 1, nullptr,
                                  nullptr, raw_interface_property_ctx, ObjectInterfacePropertyCallback, &raw_interface_property_ctx->tsfn_setter));

  napi_create_reference(env, argv[3], 1, &raw_interface_property_ctx->user_data_ref);

  interface_property_ctx.release();
  PWAOS_API_CALL_AND_RETURN(env, pwaosObjectInterface_defineProperty(interface_handle, name, ObjectInterfaceSetPropertyCallback, ObjectInterfaceGetPropertyCallback, raw_interface_property_ctx));
}

static napi_value ObjectInterfaceRemoveProperty(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: name");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle;
  napi_unwrap(env, this_arg, (void**)&interface_handle);
  if (!interface_handle) {
    napi_throw_error(env, nullptr, "Invalid interface handle");
    return nullptr;
  }

  // 获取属性名
  napi_valuetype name_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &name_type));
  if (name_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }
  size_t name_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &name_len));
  char name[256];
  if (name_len >= sizeof(name)) {
    napi_throw_error(env, nullptr, "Property name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  PWAOS_API_CALL_AND_RETURN(env, pwaosObjectInterface_removeProperty(interface_handle, name));
}

static napi_value InterfaceConstructor(napi_env env, napi_callback_info info) {
  return nullptr;
}

static napi_ref g_interface_class_ref = nullptr;
static void Init(napi_env env) {
  napi_property_descriptor methods[] = {
    DECLARE_NODE_API_PROPERTY("defineMethod", ObjectInterfaceDefineMethod),
    DECLARE_NODE_API_PROPERTY("removeMethod", ObjectInterfaceRemoveMethod),
    DECLARE_NODE_API_PROPERTY("defineProperty", ObjectInterfaceDefineProperty),
    DECLARE_NODE_API_PROPERTY("removeProperty", ObjectInterfaceRemoveProperty),
  };

  napi_value interface_class;
  NODE_API_CALL_RETURN_VOID(
    env,
    napi_define_class(env, "pwaosObjectInterface", NAPI_AUTO_LENGTH,
                      InterfaceConstructor, nullptr,
                      sizeof(methods) / sizeof(methods[0]),
                      methods, &interface_class));

  NODE_API_CALL_RETURN_VOID(env, napi_create_reference(env, interface_class, 1, &g_interface_class_ref));
}

napi_value GetOrCreateInterfaceObj(napi_env env, PWAOS_OBJECT_INTERFACE_HANDLE interface_handle) {
  if (!g_interface_class_ref)
    Init(env);

  const auto& iter = g_interfaces.find(interface_handle);
  if (iter != g_interfaces.end()) {
    napi_value obj;
    NODE_API_CALL(env, napi_get_reference_value(env, iter->second, &obj));
    return obj;
  }

  napi_value interface_class;
  NODE_API_CALL(env, napi_get_reference_value(env, g_interface_class_ref, &interface_class));

  napi_value interface_obj;
  NODE_API_CALL(env, napi_new_instance(env, interface_class, 0, 0, &interface_obj));
  napi_wrap(env, interface_obj, interface_handle, interface_obj_finalizer, nullptr, nullptr);

  napi_ref ref;
  NODE_API_CALL(env, napi_create_reference(env, interface_obj, 0, &ref));

  g_interfaces.emplace(interface_handle, ref);

  return interface_obj;
}

void ReleaseAllInterfaces(napi_env env) {
  for (auto& [iface, ref] : g_interfaces)
    napi_delete_reference(env, ref);
  g_interfaces.clear();
}