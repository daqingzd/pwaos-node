#include <node_api.h>
#include <string.h>
#include <stdio.h>

#include "common.h"
#include "instance_obj.h"

struct instance_status {
  void* userData;

  instance_status() : userData(nullptr) {}
  ~instance_status() {}
};
static std::unordered_map<PWAOS_INSTANCE_HANDLE, napi_ref> g_instances;
static std::unordered_map<PWAOS_INSTANCE_HANDLE, instance_status> g_instances_status;
instance_status* CreateInstanceStatus(PWAOS_INSTANCE_HANDLE instance_handle) {
  g_instances_status.try_emplace(instance_handle);
  const auto& iter = g_instances_status.find(instance_handle);
  return &iter->second;
}
instance_status* GetInstanceStatus(PWAOS_INSTANCE_HANDLE instance_handle) {
  const auto& iter = g_instances_status.find(instance_handle);
  if (iter != g_instances_status.end())
    return &iter->second;
  return nullptr;
}

struct InstanceCallbackContext {
  napi_threadsafe_function tsfn = nullptr;
  napi_ref user_data_ref = nullptr;

  void release(napi_env env) {
    if (tsfn)
      napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
    if (user_data_ref)
      napi_delete_reference(env, user_data_ref);
  }
};

static void InstanceStatusCallback(const pwaosInstanceStatus stat, void* userData) {
  auto* callback_ctx = (InstanceCallbackContext*)userData;
//  napi_acquire_threadsafe_function(callback_ctx->tsfn);
  napi_call_threadsafe_function(callback_ctx->tsfn, (void*)stat, napi_tsfn_blocking);
}

static void CallInstanceStatusCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  napi_value recv;
  if (napi_get_undefined(env, &recv) != napi_ok) {
    napi_throw_error(env,
                     "ERR_NAPI_TSFN_GET_UNDEFINED",
                     "Failed to retrieve undefined value");
    return;
  }

  auto status = (pwaosInstanceStatus)(intptr_t)data;
  napi_value arg;
  napi_create_int32(env, status, &arg);

  // 如果有 userData，还需要取出
  napi_value user_data;
  InstanceCallbackContext* ctx = (InstanceCallbackContext*)context;
  napi_get_reference_value(env, ctx->user_data_ref, &user_data);
  napi_value args[2] = { arg, user_data };
  napi_call_function(env, recv, js_cb, 2, args, nullptr);
}

static void instance_obj_finalizer(napi_env env,
                              void* finalize_data,
                              void* finalize_hint) {
  PWAOS_INSTANCE_HANDLE instance_handle = (PWAOS_INSTANCE_HANDLE)finalize_data;
  if (!instance_handle)
    return;

  pwaos_destroyInstance(instance_handle);

  const auto& it = g_instances.find(instance_handle);
  if (it != g_instances.end()) {
    napi_delete_reference(env, it->second);
    g_instances.erase(it);
  }

  if (auto* userData = g_instances_status[instance_handle].userData) {
    auto* callback_ctx = (InstanceCallbackContext*)userData;
    callback_ctx->release(env);
    delete callback_ctx;
  }
  g_instances_status.erase(instance_handle);
}

// 将 pwaosInstanceProperties 转换为 JS 对象
static napi_value instance_properties_to_js(napi_env env, const pwaosInstanceProperties* props) {
  napi_value obj;
  napi_create_object(env, &obj);

  // runtimeVersion
  char version[64];

  snprintf(version,
           sizeof(version),
           "%u.%u.%u",
           PWAOS_RUNTIME_API_VERSION_MAJOR(
               props->runtimeVersion),
           PWAOS_RUNTIME_API_VERSION_MINOR(
               props->runtimeVersion),
           PWAOS_RUNTIME_API_VERSION_PATCH(
               props->runtimeVersion));

  napi_value ver;
  napi_create_string_utf8(env,
                          version,
                          NAPI_AUTO_LENGTH,
                          &ver);

  napi_set_named_property(env,
                          obj,
                          "runtimeVersion",
                          ver);

  // runtimeName 作为字符串
  napi_value name;
  napi_create_string_utf8(env, props->runtimeName, NAPI_AUTO_LENGTH, &name);
  napi_set_named_property(env, obj, "runtimeName", name);

  // 可选：也可返回 type 和 next，但通常不需要，这里忽略
  return obj;
}

static napi_value InstanceGetProperties(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, nullptr, &this_arg, nullptr));

  // 从 this 对象中取出 instance 句柄
  PWAOS_INSTANCE_HANDLE instance_handle;
  napi_unwrap(env, this_arg, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance handle");
    return nullptr;
  }

  // 准备输出结构体
  pwaosInstanceProperties props;
  memset(&props, 0, sizeof(props));
  props.type = PWAOS_TYPE_INSTANCE_GET_PROPERTIES;
  props.next = nullptr;

  PWAOS_API_CALL(env, pwaosInstance_getProperties(instance_handle, &props));

  // 返回 JS 对象
  return instance_properties_to_js(env, &props);
}

static void ObjectInterfaceConstructorCallback(void* userData) {
  auto* callback_ctx = (InstanceCallbackContext*)userData;
  napi_call_threadsafe_function(callback_ctx->tsfn, nullptr, napi_tsfn_blocking);
}
static void ObjectInterfaceDestructorCallback(void* userData) {
  auto* callback_ctx = (InstanceCallbackContext*)userData;
  napi_call_threadsafe_function(callback_ctx->tsfn, nullptr, napi_tsfn_blocking);
}
static void ObjectInterfaceCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  napi_value recv;
  if (napi_get_undefined(env, &recv) != napi_ok) {
    napi_throw_error(env,
                     "ERR_NAPI_TSFN_GET_UNDEFINED",
                     "Failed to retrieve undefined value");
    return;
  }

  // 如果有 userData，还需要取出
  napi_value user_data;
  InstanceCallbackContext* ctx = (InstanceCallbackContext*)context;
  napi_value args[1];
  size_t argc = 0;
  if (napi_ok == napi_get_reference_value(env, ctx->user_data_ref, &user_data)) {
    args[0] = user_data;
    argc = 1;
  }
  napi_call_function(env, recv, js_cb, argc, args, nullptr);
}

static napi_value InstanceCreateObjectInterface(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: createInfo");
    return nullptr;
  }

  // 获取 instance handle
  PWAOS_INSTANCE_HANDLE instance_handle = nullptr;
  napi_unwrap(env, this_arg, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance handle");
    return nullptr;
  }

  // 解析 createInfo 对象
  napi_valuetype create_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &create_type));
  if (create_type != napi_object) {
    napi_throw_type_error(env, nullptr, "Second argument must be an object");
    return nullptr;
  }

  pwaosObjectInterfaceCreateInfo c_info;
  memset(&c_info, 0, sizeof(c_info));
  c_info.type = PWAOS_TYPE_OBJECT_INTERFACE_CREATE_INFO;
  c_info.next = nullptr;

  napi_value constructor_val;
  std::unique_ptr<InstanceCallbackContext> constructor_ctx;
  if (napi_get_named_property(env, argv[0], "constructor", &constructor_val) == napi_ok) {
    napi_valuetype cons_type;
    NODE_API_CALL(env, napi_typeof(env, constructor_val, &cons_type));
    if (cons_type != napi_function && cons_type != napi_null && cons_type != napi_undefined) {
      napi_throw_type_error(env, nullptr, "'constructor' must be an function, null, or undefined");
      return nullptr;
    }
    constructor_ctx = std::make_unique<InstanceCallbackContext>();
    if (!constructor_ctx) {
      napi_throw_error(env, nullptr, "Out of memory!");
      return nullptr;
    }
    auto* raw_constructor_ctx = constructor_ctx.get();

    napi_value async_name;
    NODE_API_CALL(env, napi_create_string_utf8(env, "ObjectInterfaceDestructorCallback", NAPI_AUTO_LENGTH, &async_name));
    NODE_API_CALL(env, napi_create_threadsafe_function(env, constructor_val, nullptr, async_name, 0, 1, nullptr,
                                    nullptr, raw_constructor_ctx, ObjectInterfaceCallback, &raw_constructor_ctx->tsfn));

    napi_value cons_user_val;
    if (napi_get_named_property(env, argv[0], "constructorUserData", &cons_user_val) != napi_ok) {
      napi_create_reference(env, cons_user_val, 1, &raw_constructor_ctx->user_data_ref);
    }

    c_info.constructor = ObjectInterfaceConstructorCallback;
    c_info.constructorUserData = raw_constructor_ctx;
  } else {
    c_info.constructor = nullptr;
  }

  napi_value destructor_val;
  std::unique_ptr<InstanceCallbackContext> destructor_ctx;
  if (napi_get_named_property(env, argv[0], "destructor", &destructor_val) == napi_ok) {
    napi_valuetype dest_type;
    NODE_API_CALL(env, napi_typeof(env, destructor_val, &dest_type));
    if (dest_type != napi_function && dest_type != napi_null && dest_type != napi_undefined) {
      napi_throw_type_error(env, nullptr, "'destructor' must be an function, null, or undefined");
      return nullptr;
    }
    destructor_ctx = std::make_unique<InstanceCallbackContext>();
    if (!destructor_ctx) {
      napi_throw_error(env, nullptr, "Out of memory!");
      return nullptr;
    }
    auto* raw_destructor_ctx = destructor_ctx.get();

    napi_value async_name;
    NODE_API_CALL(env, napi_create_string_utf8(env, "ObjectInterfaceDestructorCallback", NAPI_AUTO_LENGTH, &async_name));
    NODE_API_CALL(env, napi_create_threadsafe_function(env, destructor_val, nullptr, async_name, 0, 1, nullptr,
                                    nullptr, raw_destructor_ctx, ObjectInterfaceCallback, &raw_destructor_ctx->tsfn));

    napi_value dest_user_val;
    if (napi_get_named_property(env, argv[0], "destructorUserData", &dest_user_val) == napi_ok) {
      napi_create_reference(env, dest_user_val, 1, &raw_destructor_ctx->user_data_ref);
    }

    c_info.constructor = ObjectInterfaceDestructorCallback;
    c_info.constructorUserData = raw_destructor_ctx;
  } else {
    c_info.destructor = nullptr;
  }

  PWAOS_OBJECT_INTERFACE_HANDLE obj_handle = nullptr;
  constructor_ctx.release();
  destructor_ctx.release();
  PWAOS_API_CALL(env, pwaosInstance_createObjectInterface(instance_handle, &c_info, &obj_handle));

  return GetOrCreateInterfaceObj(env, obj_handle);
}

static napi_value InstanceDestroyObjectInterface(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument: interface object");
    return nullptr;
  }

  // 从 argv[0] 对象中取出 interface 句柄
  PWAOS_OBJECT_INTERFACE_HANDLE interface_handle;
  napi_unwrap(env, argv[0], (void**)&interface_handle);
  if (!interface_handle) {
    napi_throw_error(env, nullptr, "Invalid interface handle");
    return nullptr;
  }

  PWAOS_API_CALL_AND_RETURN(env, pwaosInstance_destroyObjectInterface(interface_handle));
}

// 封装 pwaosInstance_setCallback
static napi_value InstanceSetCallback(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "Expected 2 arguments: callback, userData");
    return nullptr;
  }

  // 获取 instance handle
  PWAOS_INSTANCE_HANDLE instance_handle = nullptr;
  napi_unwrap(env, this_arg, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance handle");
    return nullptr;
  }

  auto* instance_status = GetInstanceStatus(instance_handle);
  if (!instance_status) {
    napi_throw_error(env, nullptr, "Internal error");
    return nullptr;
  }

  auto* last_callback_ctx = (InstanceCallbackContext*)instance_status->userData;

  // 获取 JS 回调函数
  napi_valuetype cb_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &cb_type));
  if (cb_type == napi_null || cb_type == napi_undefined) {
    napi_value ret;
    napi_create_int32(env, pwaosInstance_setCallback(instance_handle, nullptr, nullptr), &ret);
    if (last_callback_ctx) {
      instance_status->userData = nullptr;
      last_callback_ctx->release(env);
      delete last_callback_ctx;
    }
    return ret;
  } else if (cb_type != napi_function) {
    napi_throw_type_error(env, nullptr, "First argument must be a function");
    return nullptr;
  }

  // 获取 userData（可选）
  napi_value user_data;
  if (argc >= 2) {
    user_data = argv[1];
  } else {
    napi_get_undefined(env, &user_data);
  }

  auto ctx = std::make_unique<InstanceCallbackContext>();
  if (!ctx) {
    napi_throw_type_error(env, nullptr, "Out Of Memory!!!");
    return nullptr;
  }
  auto* raw_ctx = ctx.get();

  napi_create_reference(env, user_data, 1, &raw_ctx->user_data_ref);

  // 创建线程安全函数
  napi_value async_name;
  napi_create_string_utf8(env, "InstanceStatusCallback", NAPI_AUTO_LENGTH, &async_name);
  napi_create_threadsafe_function(env, argv[0], nullptr, async_name, 0, 1, nullptr,
                                  nullptr, raw_ctx, CallInstanceStatusCallback, &raw_ctx->tsfn);

  // 注册 C 回调
  pwaosResult result = pwaosInstance_setCallback(instance_handle, InstanceStatusCallback, raw_ctx);
  if (result != PWAOS_RESULT_SUCCESS) {
    // 失败时清理
    raw_ctx->release(env);
    throw_pwaos_error(env, result);
    return nullptr;
  }

  if (last_callback_ctx) {
    last_callback_ctx->release(env);
    free(last_callback_ctx);
    instance_status->userData = nullptr;
  }

  instance_status->userData = raw_ctx;
  ctx.release();

  napi_value ret;
  NODE_API_CALL(env, napi_create_int32(env, result, &ret));
  return ret;
}

static napi_value CreatePwa(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: createInfo");
    return nullptr;
  }

  // 获取 instance handle
  PWAOS_INSTANCE_HANDLE instance_handle = nullptr;
  napi_unwrap(env, this_arg, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance handle");
    return nullptr;
  }

  // 解析 createInfo 对象
  napi_valuetype create_info_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &create_info_type));
  if (create_info_type != napi_object) {
    napi_throw_type_error(env, nullptr, "Second argument must be an object");
    return nullptr;
  }

  pwaosAppCreateInfo c_info = {
    .type = PWAOS_TYPE_PWA_CREATE_INFO,
    .next = nullptr,
  };

  // 读取 type 字段（必填）
  napi_value type_val;
  if (napi_get_named_property(env, argv[0], "type", &type_val) == napi_ok) {
    int32_t type_int;
    NODE_API_CALL(env, napi_get_value_int32(env, type_val, &type_int));
    c_info.type = (pwaosStructureType)type_int;
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'type' field in createInfo");
    return nullptr;
  }

  // 读取 appType 字段（必填）
  napi_value app_type_val;
  if (napi_get_named_property(env, argv[0], "appType", &app_type_val) == napi_ok) {
    int32_t app_type_int;
    NODE_API_CALL(env, napi_get_value_int32(env, app_type_val, &app_type_int));
    c_info.appType = (pwaosAppType)app_type_int;
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'appType' field in createInfo");
    return nullptr;
  }

  // 读取 appUrl 字段（必填）
  napi_value url_val;
  if (napi_get_named_property(env, argv[0], "appUrl", &url_val) == napi_ok) {
    napi_valuetype url_type;
    NODE_API_CALL(env, napi_typeof(env, url_val, &url_type));
    if (url_type != napi_string) {
      napi_throw_type_error(env, nullptr, "'appUrl' must be a string");
      return nullptr;
    }
    size_t url_len;
    NODE_API_CALL(env, napi_get_value_string_utf8(env, url_val, (char*)c_info.appUrl, sizeof(c_info.appUrl), &url_len));
    if (url_len >= PWAOS_MAX_URL_LEN) {
      napi_throw_error(env, nullptr, "appUrl too long");
      return nullptr;
    }
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'appUrl' field in createInfo");
    return nullptr;
  }

  // 读取 width（可选）
  napi_value width_val;
  if (napi_get_named_property(env, argv[0], "width", &width_val) == napi_ok) {
    NODE_API_CALL(env, napi_get_value_int32(env, width_val, &c_info.width));
  } else {
    c_info.width = 0;
  }

  // 读取 height（可选）
  napi_value height_val;
  if (napi_get_named_property(env, argv[0], "height", &height_val) == napi_ok) {
    NODE_API_CALL(env, napi_get_value_int32(env, height_val, &c_info.height));
  } else {
    c_info.height = 0;
  }

  // 调用底层 C API
  PWAOS_APP_HANDLE app_handle = nullptr;
  pwaosResult result = pwaosInstance_createPwa(instance_handle, &c_info, &app_handle);

  // 处理结果
  if (result != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, result);
    return nullptr;
  }

  return GetOrCreateAppObj(env, app_handle);
}

static napi_value DestroyPwa(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument (appHandle)");
    return nullptr;
  }

  const auto& app_val = argv[0];

  // 从 argv[0] 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, app_val, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid app object");
    return nullptr;
  }

  PWAOS_API_CLEAR_CALLBACK(env, app_val, "setCallback");
  pwaosResult result = pwaosInstance_destroyPwa(app_handle);
  if (result != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, result);
    return nullptr;
  }

  // 成功返回 result 数值
  napi_value ret;
  NODE_API_CALL(env, napi_create_int32(env, result, &ret));
  return ret;
}

// 辅助函数：将 pwaosEventDataInstanceStatusChanged 转换为 JS 对象
static napi_value BuildInstanceStatusEvent(napi_env env,
                                           const pwaosEventDataInstanceStatusChanged& event) {
  napi_value obj;
  NODE_API_CALL(env, napi_create_object(env, &obj));

  // 添加 type 字段
  napi_value type_val;
  NODE_API_CALL(env, napi_create_int32(env, PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED, &type_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "type", type_val));

  // instance 字段
  NODE_API_CALL(env, napi_set_named_property(env, obj, "instance", GetOrCreateInstanceObj(env, event.instance)));

  // status 枚举值
  napi_value status_val;
  NODE_API_CALL(env, napi_create_int32(env, event.status, &status_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "status", status_val));

  // time（int64）
  napi_value time_val;
  NODE_API_CALL(env, napi_create_int64(env, event.time, &time_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "time", time_val));

  return obj;
}

// 辅助函数：将 pwaosEventDataAppStatusChanged 转换为 JS 对象
static napi_value BuildAppStatusEvent(napi_env env,
                                      const pwaosEventDataAppStatusChanged& event) {
  napi_value obj;
  NODE_API_CALL(env, napi_create_object(env, &obj));

  napi_value type_val;
  NODE_API_CALL(env, napi_create_int32(env, PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED, &type_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "type", type_val));

  NODE_API_CALL(env, napi_set_named_property(env, obj, "app", GetOrCreateAppObj(env, event.app)));

  napi_value status_val;
  NODE_API_CALL(env, napi_create_int32(env, event.status, &status_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "status", status_val));

  napi_value time_val;
  NODE_API_CALL(env, napi_create_int64(env, event.time, &time_val));
  NODE_API_CALL(env, napi_set_named_property(env, obj, "time", time_val));

  return obj;
}

// 解析事件缓冲区，返回 JS 对象
static napi_value ParseEventData(napi_env env, pwaosEventDataBuffer* buffer) {
  switch (buffer->type) {
    case PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED: {
      return BuildInstanceStatusEvent(env, reinterpret_cast<pwaosEventDataInstanceStatusChanged&>(*buffer));
    }
    case PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED: {
      return BuildAppStatusEvent(env, reinterpret_cast<pwaosEventDataAppStatusChanged&>(*buffer));
    }
    default:
      // 未知事件类型，返回 undefined
      napi_value undefined;
      napi_get_undefined(env, &undefined);
      return undefined;
  }
}

static napi_value PollEvent(napi_env env, napi_callback_info info) {
  // 获取 this 对象并 unwrap 出 instance handle
  napi_value this_arg;
  size_t argc = 0;
  napi_value* argv = nullptr;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  // 获取 instance handle
  PWAOS_INSTANCE_HANDLE instance_handle = nullptr;
  napi_unwrap(env, this_arg, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance handle");
    return nullptr;
  }

  pwaosEventDataBuffer buffer;
  pwaosResult result = pwaosInstance_pollEvent(instance_handle, &buffer);
  switch (result) {
    case PWAOS_RESULT_SUCCESS:
      return ParseEventData(env, &buffer);

    case PWAOS_RESULT_EVENT_UNAVAILABLE:
      // 无事件，返回 null
      napi_value null_val;
      napi_get_null(env, &null_val);
      return null_val;

    default: {
      // 其他错误，抛出异常
      char msg[PWAOS_MAX_RESULT_NAME_SIZE];
      pwaos_resultToString(result, msg);
      napi_throw_error(env, nullptr, msg);
      return nullptr;
    }
  }
}

static napi_value InstanceConstructor(napi_env env, napi_callback_info info) {
  return nullptr;
}

static napi_ref g_instance_class_ref = nullptr;
napi_value InitInstanceClass(napi_env env, napi_value exports) {
  napi_property_descriptor methods[] = {
    DECLARE_NODE_API_PROPERTY("createPwa", CreatePwa),
    DECLARE_NODE_API_PROPERTY("destroyPwa", DestroyPwa),
    DECLARE_NODE_API_PROPERTY("createObjectInterface", InstanceCreateObjectInterface),
    DECLARE_NODE_API_PROPERTY("destroyObjectInterface", InstanceDestroyObjectInterface),
    DECLARE_NODE_API_PROPERTY("getProperties", InstanceGetProperties),
    DECLARE_NODE_API_PROPERTY("pollEvent", PollEvent),
    DECLARE_NODE_API_PROPERTY("setCallback", InstanceSetCallback),
  };

  napi_value instance_class;
  NODE_API_CALL(
    env,
    napi_define_class(env, "pwaosInstance", NAPI_AUTO_LENGTH,
                      InstanceConstructor, nullptr,
                      sizeof(methods) / sizeof(methods[0]),
                      methods, &instance_class));

  NODE_API_CALL(env, napi_create_reference(env, instance_class, 1, &g_instance_class_ref));

  DefineStaticInt32Property(env, instance_class, "instanceStatusUnavailable", instanceStatusUnavailable);
  DefineStaticInt32Property(env, instance_class, "instanceStatusAvailable", instanceStatusAvailable);
  DefineStaticInt32Property(env, instance_class, "instanceStatusStarting", instanceStatusStarting);
  DefineStaticInt32Property(env, instance_class, "instanceStatusWorking", instanceStatusWorking);
  DefineStaticInt32Property(env, instance_class, "instanceStatusStopping", instanceStatusStopping);
  DefineStaticInt32Property(env, instance_class, "instanceStatusConnectionLost", instanceStatusConnectionLost);

  // pwaosAppType
  DefineStaticInt32Property(env, instance_class, "appTypeUnknown", appTypeUnknown);
  DefineStaticInt32Property(env, instance_class, "appTypePwa", appTypePwa);
  DefineStaticInt32Property(env, instance_class, "appTypeBrowser", appTypeBrowser);
  DefineStaticInt32Property(env, instance_class, "appTypeConsole", appTypeConsole);
  DefineStaticInt32Property(env, instance_class, "appTypeDesktop", appTypeDesktop);
  DefineStaticInt32Property(env, instance_class, "appTypePwaContents", appTypePwaContents);

  // pwaosAppStatus
  DefineStaticInt32Property(env, instance_class, "appStatusInVisible", appStatusInVisible);
  DefineStaticInt32Property(env, instance_class, "appStatusStopping", appStatusStopping);
  DefineStaticInt32Property(env, instance_class, "appStatusReady", appStatusReady);
  DefineStaticInt32Property(env, instance_class, "appStatusSynchronized", appStatusSynchronized);
  DefineStaticInt32Property(env, instance_class, "appStatusVisible", appStatusVisible);
  DefineStaticInt32Property(env, instance_class, "appStatusFocused", appStatusFocused);
  DefineStaticInt32Property(env, instance_class, "appStatusUnFocused", appStatusUnFocused);

  return instance_class;
}

napi_value GetOrCreateInstanceObj(napi_env env, PWAOS_INSTANCE_HANDLE instance_handle) {
  const auto& iter = g_instances.find(instance_handle);
  if (iter != g_instances.end()) {
    napi_value obj;
    NODE_API_CALL(env, napi_get_reference_value(env, iter->second, &obj));
    return obj;
  }

  napi_value instance_class;
  NODE_API_CALL(env, napi_get_reference_value(env, g_instance_class_ref, &instance_class));

  napi_value instance_obj;
  NODE_API_CALL(env, napi_new_instance(env, instance_class, 0, 0, &instance_obj));
  napi_wrap(env, instance_obj, instance_handle, instance_obj_finalizer, nullptr, nullptr);

  napi_ref ref;
  NODE_API_CALL(env, napi_create_reference(env, instance_obj, 0, &ref));

  g_instances.emplace(instance_handle, ref);

  CreateInstanceStatus(instance_handle);
  return instance_obj;
}

void ReleaseAllInstances(napi_env env) {
  for (auto& [instance, ref] : g_instances)
    napi_delete_reference(env, ref);
  g_instances.clear();

  for (auto& [instance, instance_status] : g_instances_status)
  if (instance_status.userData) {
    auto* callback_ctx = (InstanceCallbackContext*)instance_status.userData;
    callback_ctx->release(env);
    delete callback_ctx;
  }
  g_instances_status.clear();
}