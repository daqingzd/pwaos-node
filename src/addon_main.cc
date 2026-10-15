#include <iostream>
#include <uv.h>

#include "common.h"
#include "instance_obj.h"

static uv_timer_t handle_events_timer;
static void OnHandleEvents(uv_timer_t* handle) {
  pwaosInstance_pollEvent(nullptr, nullptr);
}

static void StartHandleEventsTimer(napi_env env) {
  uv_loop_t* loop = nullptr;
  napi_get_uv_event_loop(env, &loop);
  uv_timer_init(loop, &handle_events_timer);
  uv_timer_start(
    &handle_events_timer,
    OnHandleEvents,
    100, // first fire
    100  // repeat
  );
}

static void StopHandleEventsTimer() {
  uv_timer_stop(&handle_events_timer);
}

static void CallPwaosCallback(pwaosResult, void* userData) {
  pwaosPromiseData_tsfn* promise = (pwaosPromiseData_tsfn*)userData;
  napi_call_threadsafe_function(promise->tsfn, promise, napi_tsfn_blocking);
  napi_release_threadsafe_function(promise->tsfn, napi_tsfn_release);
}

static napi_value SetRuntimeExecutablePath(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument (runtime_path)");
    return nullptr;
  }

  napi_valuetype valuetype;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &valuetype));
  if (valuetype != napi_string) {
    napi_throw_type_error(env, nullptr, "First argument must be a string");
    return nullptr;
  }

  char buffer[PWAOS_MAX_FILE_PATH_LEN];
  size_t copied;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], buffer, sizeof(buffer), &copied));

  PWAOS_API_CALL_AND_RETURN(env, pwaos_setRuntimeExecutablePath(buffer));
}

napi_value InitPwaos(napi_env env, napi_callback_info info) {
  auto data = PWAOS_API_MAKE_TSFN_PROMISE(env, nullptr, {}, pwaosPromiseData::CompleteCallback);
  if (!data) {
    napi_throw_error(env, nullptr, "Failed to create threadsafe function");
    return nullptr;
  }

  pwaosAsyncCallbackInfo callbackInfo = {
    .type = PWAOS_TYPE_ASYNC_CALLBACK_INFO,
    .next = nullptr,
    .callback = CallPwaosCallback,
    .userData = data.get(),
  };
  pwaosInitializeInfo initializeInfo = {
    .type = PWAOS_TYPE_INITIALIZE_INFO,
    .next = &callbackInfo,
    .argc = 0,
    .argv = nullptr,
    .use_dedicated_thread = false,
  };

  auto result = pwaos_init(&initializeInfo);
  if (result == PWAOS_RESULT_PENDING) {
    return data.release()->promise;
  }

  PWAOS_API_CALL_AND_RETURN(env, result);
}

static napi_value ShutDownPwaos(napi_env env, napi_callback_info info) {
  // Release all the instances
  StopHandleEventsTimer();
  ReleaseAllInterfaces(env);
  ReleaseAllApps(env);
  ReleaseAllInstances(env);

  PWAOS_API_CALL_AND_RETURN(env, pwaos_shutDown());
}

static napi_value ResultToString(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));
  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument (result code)");
    return nullptr;
  }

  int32_t result_val;
  NODE_API_CALL(env, napi_get_value_int32(env, argv[0], &result_val));

  char buffer[PWAOS_MAX_RESULT_NAME_SIZE];
  pwaosResult c_result = (pwaosResult)result_val;
  pwaosResult ret = pwaos_resultToString(c_result, buffer);
  if (ret != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, ret);
    return nullptr;
  }

  napi_value js_str;
  NODE_API_CALL(env, napi_create_string_utf8(env, buffer, NAPI_AUTO_LENGTH, &js_str));
  return js_str;
}

static napi_value GetCommandLine(napi_env env, napi_callback_info info) {
  napi_value js_str;
  NODE_API_CALL(env,
                napi_create_string_utf8(env, pwaos_getCommandLine(), NAPI_AUTO_LENGTH, &js_str));
  return js_str;
}

using CreateInstanceArgsPair = std::pair<PWAOS_INSTANCE_HANDLE, pwaosPromiseData_tsfn*>;
static void CallCreateInstanceCallback(pwaosResult, void* userData) {
  auto* args_pair = (CreateInstanceArgsPair*)userData;
  napi_call_threadsafe_function(args_pair->second->tsfn, args_pair->first, napi_tsfn_blocking);
  napi_release_threadsafe_function(args_pair->second->tsfn, napi_tsfn_release);
}
static void CreateInstance_CompleteCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  auto* d = (CreateInstanceArgsPair*)data;
  auto* p = static_cast<pwaosPromiseData*>(context);
  p->result.node_val = GetOrCreateInstanceObj(env, d->first);
  p->result.have_node_result = true;
  return pwaosPromiseData::CompleteCallback(env, js_cb, context, data);
}
static napi_value CreateInstance(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument (createInfo)");
    return nullptr;
  }

  napi_valuetype create_info_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &create_info_type));
  if (create_info_type != napi_object) {
    napi_throw_type_error(env, nullptr, "Argument must be an object");
    return nullptr;
  }

  pwaosInstanceCreateInfo instanceCreateInfo = {
    .type = PWAOS_TYPE_INSTANCE_CREATE_INFO,
    .next = nullptr,
  };

  // 读取 type 字段（必填）
  napi_value type_val;
  if (napi_get_named_property(env, argv[0], "type", &type_val) == napi_ok) {
    int32_t type_int;
    NODE_API_CALL(env, napi_get_value_int32(env, type_val, &type_int));
    instanceCreateInfo.type = (pwaosStructureType)type_int;
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'type' field in createInfo");
    return nullptr;
  }

  // 读取 runtimeType 字段（必填）
  napi_value rt_val;
  if (napi_get_named_property(env, argv[0], "runtimeType", &rt_val) == napi_ok) {
    int32_t rt_int;
    NODE_API_CALL(env, napi_get_value_int32(env, rt_val, &rt_int));
    instanceCreateInfo.runtimeType = (pwaosRuntimeType)rt_int;
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'runtimeType' field in createInfo");
    return nullptr;
  }

  // 读取 serverUrl 字段（必填）
  napi_value url_val;
  if (napi_get_named_property(env, argv[0], "serverUrl", &url_val) == napi_ok) {
    napi_valuetype url_type;
    NODE_API_CALL(env, napi_typeof(env, url_val, &url_type));
    if (url_type != napi_string) {
      napi_throw_type_error(env, nullptr, "'serverUrl' must be a string");
      return nullptr;
    }
    size_t url_len;
    NODE_API_CALL(env, napi_get_value_string_utf8(env, url_val, (char*)instanceCreateInfo.serverUrl, sizeof(instanceCreateInfo.serverUrl), &url_len));
    if (url_len >= PWAOS_MAX_URL_LEN) {
      napi_throw_error(env, nullptr, "serverUrl too long");
      return nullptr;
    }
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'serverUrl' field in createInfo");
    return nullptr;
  }

  // 读取 dataPath 字段（可选，可传空字符串或省略）
  napi_value path_val;
  if (napi_get_named_property(env, argv[0], "dataPath", &path_val) == napi_ok) {
    napi_valuetype path_type;
    NODE_API_CALL(env, napi_typeof(env, path_val, &path_type));
    if (path_type == napi_string) {
      size_t path_len;
      NODE_API_CALL(env, napi_get_value_string_utf8(env, path_val, nullptr, 0, &path_len));
      if (path_len >= PWAOS_MAX_FILE_PATH_LEN) {
        napi_throw_error(env, nullptr, "dataPath too long");
        return nullptr;
      }
      NODE_API_CALL(env, napi_get_value_string_utf8(env, path_val, instanceCreateInfo.dataPath, sizeof(instanceCreateInfo.dataPath), &path_len));
    } else if (path_type != napi_undefined && path_type != napi_null) {
      napi_throw_type_error(env, nullptr, "'dataPath' must be a string or omitted");
      return nullptr;
    }
  }

  auto data = PWAOS_API_MAKE_TSFN_PROMISE(env, nullptr, {}, CreateInstance_CompleteCallback);
  if (!data) {
    napi_throw_error(env, nullptr, "Failed to create threadsafe function");
    return nullptr;
  }

  auto p = std::make_unique<CreateInstanceArgsPair>(nullptr, data.get());
  if (!p) {
    napi_throw_error(env, nullptr, "Out of memory!");
    return nullptr;
  }
  auto* raw_p = p.get();

  pwaosAsyncCallbackInfo callbackInfo = {
    .type = PWAOS_TYPE_ASYNC_CALLBACK_INFO,
    .next = nullptr,
    .callback = CallCreateInstanceCallback,
    .userData = raw_p,
  };
  instanceCreateInfo.next = &callbackInfo;

  auto result = pwaos_createInstance(&instanceCreateInfo, &raw_p->first);
  if (result != PWAOS_RESULT_SUCCESS) {
    return nullptr;
  }

  if (result == PWAOS_RESULT_PENDING) {
    p.release();
    return data.release()->promise;
  }

  return GetOrCreateInstanceObj(env, raw_p->first);
}

static napi_value DestroyInstance(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument (instance object)");
    return nullptr;
  }

  const auto& instance_val = argv[0];

  // 获取 instance handle
  PWAOS_INSTANCE_HANDLE instance_handle = nullptr;
  napi_unwrap(env, instance_val, (void**)&instance_handle);
  if (!instance_handle) {
    napi_throw_error(env, nullptr, "Invalid instance object");
    return nullptr;
  }

  PWAOS_API_CALL_AND_RETURN(env, pwaos_destroyInstance(instance_handle));
}

static void AddonCleanup(void* data) {}

napi_value Init(napi_env env, napi_value exports) {
  napi_property_descriptor properties[] = {
    DECLARE_NODE_API_PROPERTY("createInstance", CreateInstance),
    DECLARE_NODE_API_PROPERTY("destroyInstance", DestroyInstance),
    DECLARE_NODE_API_PROPERTY("init", InitPwaos),
    DECLARE_NODE_API_PROPERTY("getCommandLine", GetCommandLine),
    DECLARE_NODE_API_PROPERTY("resultToString", ResultToString),
    DECLARE_NODE_API_PROPERTY("setRuntimeExecutablePath", SetRuntimeExecutablePath),
    DECLARE_NODE_API_PROPERTY("shutDown", ShutDownPwaos),

    DECLARE_NODE_API_PROPERTY_VALUE("Instance", InitInstanceClass(env, exports)),
    DECLARE_NODE_API_PROPERTY_VALUE("App", InitAppClass(env, exports)),
  };

  NODE_API_CALL(env, napi_define_properties(
    env, exports, sizeof(properties) / sizeof(*properties), properties));

  // pwaosRuntimeType
  DefineStaticInt32Property(env, exports, "runtimeTypeUnknown", runtimeTypeUnknown);
  DefineStaticInt32Property(env, exports, "runtimeTypePwa", runtimeTypePwa);

  // pwaosStructureType
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_UNKNOWN", PWAOS_TYPE_UNKNOWN);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_INITIALIZE_INFO", PWAOS_TYPE_INITIALIZE_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_INSTANCE_CREATE_INFO", PWAOS_TYPE_INSTANCE_CREATE_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_INSTANCE_GET_PROPERTIES", PWAOS_TYPE_INSTANCE_GET_PROPERTIES);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PWA_CREATE_INFO", PWAOS_TYPE_PWA_CREATE_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_INFO", PWAOS_TYPE_PROJECTOR_CREATE_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_NATIVE_INFO_EXT", PWAOS_TYPE_PROJECTOR_CREATE_NATIVE_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_EGL_INFO_EXT", PWAOS_TYPE_PROJECTOR_CREATE_EGL_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_VULKAN_INFO_EXT", PWAOS_TYPE_PROJECTOR_CREATE_VULKAN_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_D3D11_INFO_EXT", PWAOS_TYPE_PROJECTOR_CREATE_D3D11_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_CREATE_D3D12_INFO_EXT", PWAOS_TYPE_PROJECTOR_CREATE_D3D12_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_BITMAP_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_BITMAP_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_EGL_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_EGL_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_VULKAN_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_VULKAN_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_D3D11_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_D3D11_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_D3D12_DATA", PWAOS_TYPE_PROJECTOR_DRAW_CALLBACK_D3D12_DATA);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_DEVTOOLS_URI_INFO", PWAOS_TYPE_DEVTOOLS_URI_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_OBJECT_INTERFACE_CREATE_INFO", PWAOS_TYPE_OBJECT_INTERFACE_CREATE_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_WINDOW_BOUNDS_INFO", PWAOS_TYPE_WINDOW_BOUNDS_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_USER_AGENT_INFO", PWAOS_TYPE_USER_AGENT_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_INITIALIZE_ANDROID_INFO_EXT", PWAOS_TYPE_INITIALIZE_ANDROID_INFO_EXT);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_NETWORK_PROXY_INFO", PWAOS_TYPE_NETWORK_PROXY_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED", PWAOS_TYPE_EVENT_DATA_INSTANCE_STATUS_CHANGED);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED", PWAOS_TYPE_EVENT_DATA_APP_STATUS_CHANGED);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_ASYNC_CALLBACK_INFO", PWAOS_TYPE_ASYNC_CALLBACK_INFO);
  DefineStaticInt32Property(env, exports, "PWAOS_TYPE_APP_FPS_INFO", PWAOS_TYPE_APP_FPS_INFO);

  // pwaosResult
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_SUCCESS", PWAOS_RESULT_SUCCESS);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_PENDING", PWAOS_RESULT_PENDING);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_EVENT_UNAVAILABLE", PWAOS_RESULT_EVENT_UNAVAILABLE);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_INTERNAL", PWAOS_RESULT_ERROR_INTERNAL);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_RUNTIME_CONNECTION_LOST", PWAOS_RESULT_ERROR_RUNTIME_CONNECTION_LOST);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_RUNTIME_NOT_READY", PWAOS_RESULT_ERROR_RUNTIME_NOT_READY);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_VALIDATION_FAILURE", PWAOS_RESULT_ERROR_VALIDATION_FAILURE);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_HANDLE_INVALID", PWAOS_RESULT_ERROR_HANDLE_INVALID);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_NOT_IMPLEMENT", PWAOS_RESULT_ERROR_NOT_IMPLEMENT);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_NOT_SUPPORT", PWAOS_RESULT_ERROR_NOT_SUPPORT);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_DUPLICATED_NAME", PWAOS_RESULT_ERROR_DUPLICATED_NAME);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_NOT_ENOUGH_MEMORY", PWAOS_RESULT_ERROR_NOT_ENOUGH_MEMORY);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_NOT_DEFINED_NAME", PWAOS_RESULT_ERROR_NOT_DEFINED_NAME);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_DATA_PATH_NOT_ABSOLUTE", PWAOS_RESULT_ERROR_DATA_PATH_NOT_ABSOLUTE);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_COMPOSITING_NOT_SYNCHRONIZED", PWAOS_RESULT_ERROR_COMPOSITING_NOT_SYNCHRONIZED);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_INVALID_THREAD", PWAOS_RESULT_ERROR_INVALID_THREAD);
  DefineStaticInt32Property(env, exports, "PWAOS_RESULT_ERROR_MAX", PWAOS_RESULT_ERROR_MAX);

  NODE_API_CALL(env, napi_add_env_cleanup_hook(env, AddonCleanup, nullptr));

  StartHandleEventsTimer(env);
  return exports;
}

NAPI_MODULE(NODE_GYP_MODULE_NAME, Init)
