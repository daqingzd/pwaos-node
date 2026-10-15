#include <node_api.h>
#include <string.h>
#include <stdio.h>
#include <memory>

#include "common.h"

struct AppCallbackContext;
struct app_status {
  AppCallbackContext* userData;
};
static std::unordered_map<PWAOS_APP_HANDLE, napi_ref> g_apps;
static std::unordered_map<PWAOS_APP_HANDLE, app_status> g_apps_status;
static app_status* CreateAppStatus(PWAOS_APP_HANDLE app_handle) {
  g_apps_status.try_emplace(app_handle);
  const auto& iter = g_apps_status.find(app_handle);
  return &iter->second;
}
static app_status* GetAppStatus(PWAOS_APP_HANDLE app_handle) {
  const auto& iter = g_apps_status.find(app_handle);
  if (iter != g_apps_status.end())
    return &iter->second;
  return nullptr;
}

struct AppCallbackContext {
  napi_threadsafe_function tsfn = nullptr;
  napi_ref user_data_ref = nullptr;

  void release(napi_env env) {
    if (tsfn)
      napi_release_threadsafe_function(tsfn, napi_tsfn_abort);
    if (user_data_ref)
      napi_delete_reference(env, user_data_ref);
  }
};

static void AppCallback(const pwaosAppStatus stat, void* userData) {
  auto* callback_ctx = (AppCallbackContext*)userData;
//  napi_acquire_threadsafe_function(callback_ctx->tsfn);
  napi_call_threadsafe_function(callback_ctx->tsfn, (void*)stat, napi_tsfn_blocking);
}

static void CallAppStatusCallback(napi_env env, napi_value js_cb, void* context, void* data) {
  napi_value recv;
  if (napi_get_undefined(env, &recv) != napi_ok) {
    napi_throw_error(env,
                     "ERR_NAPI_TSFN_GET_UNDEFINED",
                     "Failed to retrieve undefined value");
    return;
  }

  auto status = (pwaosAppStatus)(intptr_t)data;
  napi_value arg;
  napi_create_int32(env, status, &arg);

  // 如果有 userData，还需要取出
  napi_value user_data;
  AppCallbackContext* ctx = (AppCallbackContext*)context;
  napi_value args[2];
  size_t argc = 1;
  if (napi_ok == napi_get_reference_value(env, ctx->user_data_ref, &user_data)) {
    args[1] = user_data;
    argc = 2;
  }
  napi_call_function(env, recv, js_cb, argc, args, nullptr);
}

static void app_obj_finalizer(napi_env env,
                              void* finalize_data,
                              void* finalize_hint) {
  PWAOS_APP_HANDLE app_handle = (PWAOS_APP_HANDLE)finalize_data;
  if (!app_handle)
    return;

  pwaosInstance_destroyPwa(app_handle);

  const auto& it = g_apps.find(app_handle);
  if (it != g_apps.end()) {
    napi_delete_reference(env, it->second);
    g_apps.erase(it);
  }

  if (auto* userData = g_apps_status[app_handle].userData) {
    auto* callback_ctx = (AppCallbackContext*)userData;
    callback_ctx->release(env);
    delete callback_ctx;
  }
  g_apps_status.erase(app_handle);
}

// 将 JS 对象转换为 pwaosWindowBoundsInfo
static napi_status bounds_from_js(napi_env env, napi_value js_bounds, pwaosWindowBoundsInfo* bounds) {
  napi_status status;
  memset(bounds, 0, sizeof(*bounds));
  bounds->type = PWAOS_TYPE_WINDOW_BOUNDS_INFO;
  bounds->next = nullptr;

  napi_value x_val, y_val, w_val, h_val;
  status = napi_get_named_property(env, js_bounds, "x", &x_val);
  if (status == napi_ok) status = napi_get_value_int32(env, x_val, &bounds->x);
  if (status != napi_ok) return status;
  status = napi_get_named_property(env, js_bounds, "y", &y_val);
  if (status == napi_ok) status = napi_get_value_int32(env, y_val, &bounds->y);
  if (status != napi_ok) return status;
  status = napi_get_named_property(env, js_bounds, "width", &w_val);
  if (status == napi_ok) status = napi_get_value_int32(env, w_val, &bounds->width);
  if (status != napi_ok) return status;
  status = napi_get_named_property(env, js_bounds, "height", &h_val);
  if (status == napi_ok) status = napi_get_value_int32(env, h_val, &bounds->height);
  return status;
}

// 将 pwaosWindowBoundsInfo 转换为 JS 对象
static napi_value bounds_to_js(napi_env env, const pwaosWindowBoundsInfo* bounds) {
  napi_value obj;
  napi_create_object(env, &obj);
  napi_value x, y, w, h;
  napi_create_int32(env, bounds->x, &x);
  napi_create_int32(env, bounds->y, &y);
  napi_create_int32(env, bounds->width, &w);
  napi_create_int32(env, bounds->height, &h);
  napi_set_named_property(env, obj, "x", x);
  napi_set_named_property(env, obj, "y", y);
  napi_set_named_property(env, obj, "width", w);
  napi_set_named_property(env, obj, "height", h);
  return obj;
}

static napi_value AppCreateProjector(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: createInfo");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  // 解析 createInfo 对象
  napi_valuetype create_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &create_type));
  if (create_type != napi_object) {
    napi_throw_type_error(env, nullptr, "Second argument must be an object");
    return nullptr;
  }

  pwaosProjectorCreateInfo c_info;
  memset(&c_info, 0, sizeof(c_info));
  c_info.type = PWAOS_TYPE_PROJECTOR_CREATE_INFO;
  c_info.next = nullptr;

  // 读取 projectorType
  napi_value pt_val;
  if (napi_get_named_property(env, argv[0], "projectorType", &pt_val) == napi_ok) {
    int32_t pt;
    NODE_API_CALL(env, napi_get_value_int32(env, pt_val, &pt));
    c_info.projectorType = (pwaosProjectorType)pt;
  } else {
    napi_throw_type_error(env, nullptr, "Missing 'projectorType'");
    return nullptr;
  }

  // 读取 width, height (可选)
  napi_value w_val, h_val;
  if (napi_get_named_property(env, argv[0], "width", &w_val) == napi_ok) {
    NODE_API_CALL(env, napi_get_value_int32(env, w_val, &c_info.width));
  } else {
    c_info.width = 0;
  }
  if (napi_get_named_property(env, argv[0], "height", &h_val) == napi_ok) {
    NODE_API_CALL(env, napi_get_value_int32(env, h_val, &c_info.height));
  } else {
    c_info.height = 0;
  }

  // 注意：更复杂的扩展结构（如 EGL/Vulkan/D3D）暂不处理，使用默认

  PWAOS_PROJECTOR_HANDLE projector_handle = nullptr;
  PWAOS_API_CALL(env, pwaosApp_createProjector(app_handle, &c_info, &projector_handle));

  napi_value js_handle;
  NODE_API_CALL(env, napi_create_external(env, projector_handle, nullptr, nullptr, &js_handle));
  return js_handle;
}

static napi_value AppDestroyProjector(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, nullptr, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 argument: projectorHandle");
    return nullptr;
  }

  PWAOS_PROJECTOR_HANDLE projector_handle = nullptr;
  napi_valuetype handle_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &handle_type));
  if (handle_type == napi_external) {
    NODE_API_CALL(env, napi_get_value_external(env, argv[0], (void**)&projector_handle));
  } else {
    napi_throw_type_error(env, nullptr, "Argument must be an external handle");
    return nullptr;
  }

  pwaosResult result = pwaosApp_destroyProjector(projector_handle);
  if (result != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, result);
    return nullptr;
  }

  napi_value ret;
  NODE_API_CALL(env, napi_create_int32(env, result, &ret));
  return ret;
}

static napi_value AppGetBounds(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, nullptr, &this_arg, nullptr));

  // 从 this 对象中取出 interface 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  pwaosWindowBoundsInfo bounds;
  pwaosResult result = pwaosApp_getBounds(app_handle, &bounds);
  if (result != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, result);
    return nullptr;
  }

  return bounds_to_js(env, &bounds);
}

static napi_value AppSetBounds(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: bounds");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  napi_valuetype bounds_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &bounds_type));
  if (bounds_type != napi_object) {
    napi_throw_type_error(env, nullptr, "First argument must be an object");
    return nullptr;
  }

  pwaosWindowBoundsInfo bounds;
  napi_status status = bounds_from_js(env, argv[0], &bounds);
  if (status != napi_ok) {
    napi_throw_type_error(env, nullptr, "Invalid bounds object");
    return nullptr;
  }

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_setBounds(app_handle, &bounds));
}

static napi_value AppExecuteJavaScript(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: javascript");
    return nullptr;
  }

  // 从 this 对象中取出 interface 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  napi_valuetype js_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &js_type));
  if (js_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }

  size_t js_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &js_len));
  auto js_code = std::make_unique<char[]>(js_len + 1);
  if (!js_code) {
    napi_throw_error(env, nullptr, "Out of memory");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], js_code.get(), js_len + 1, &js_len));

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_executeJavaScript(app_handle, js_code.get()));
}

static napi_value AppGetDensity(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, nullptr, &this_arg, nullptr));

  // 从 this 对象中取出 interface 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  float density;
  PWAOS_API_CALL(env, pwaosApp_getDensity(app_handle, &density));

  napi_value js_density;
  NODE_API_CALL(env, napi_create_double(env, density, &js_density));
  return js_density;
}

static napi_value AppSetDensity(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: density");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  double density;
  NODE_API_CALL(env, napi_get_value_double(env, argv[0], &density));

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_setDensity(app_handle, (float)density));
}

static napi_value AppGetDevToolsUri(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, nullptr, &this_arg, nullptr));

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  pwaosUriInfo uri_data;
  memset(&uri_data, 0, sizeof(uri_data));
  uri_data.type = PWAOS_TYPE_DEVTOOLS_URI_INFO;
  uri_data.next = nullptr;

  auto result = pwaosApp_getDevToolsUri(app_handle, &uri_data);
  if (PWAOS_RESULT_SUCCESS == result ) {
    napi_value js_uri;
    NODE_API_CALL(env, napi_create_string_utf8(env, uri_data.url, NAPI_AUTO_LENGTH, &js_uri));
    return js_uri;
  }

  napi_value ret;
  NODE_API_CALL(env, napi_create_int32(env, result, &ret));
  return ret;
}

static napi_value AppSetFPS(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: fps");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  double fps;
  NODE_API_CALL(env, napi_get_value_double(env, argv[0], &fps));
  pwaosAppFpsInfo fpsInfo{PWAOS_TYPE_APP_FPS_INFO, nullptr, static_cast<float>(fps)};
  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_setFPS(app_handle, &fpsInfo));
}

static napi_value AppHandleClick(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "Expected 2 arguments: isLeft, isClicked");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  bool is_left, is_clicked;
  NODE_API_CALL(env, napi_get_value_bool(env, argv[0], &is_left));
  NODE_API_CALL(env, napi_get_value_bool(env, argv[1], &is_clicked));

  pwaosResult result = pwaosApp_handleClick(app_handle, is_left, is_clicked);
  if (result != PWAOS_RESULT_SUCCESS) {
    throw_pwaos_error(env, result);
    return nullptr;
  }

  napi_value ret;
  NODE_API_CALL(env, napi_create_int32(env, result, &ret));
  return ret;
}

static napi_value AppHandlePointerInput(napi_env env, napi_callback_info info) {
  size_t argc = 3;
  napi_value argv[3];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 3) {
    napi_throw_type_error(env, nullptr, "Expected 3 arguments: isLeft, x, y");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  bool is_left;
  double x, y;
  NODE_API_CALL(env, napi_get_value_bool(env, argv[0], &is_left));
  NODE_API_CALL(env, napi_get_value_double(env, argv[1], &x));
  NODE_API_CALL(env, napi_get_value_double(env, argv[2], &y));

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_handlePointerInput(app_handle, is_left, (float)x, (float)y));
}

static napi_value AppGetUserAgent(napi_env env, napi_callback_info info) {
  size_t argc = 0;
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, nullptr, &this_arg, nullptr));

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  pwaosUserAgentInfo ua_data;
  memset(&ua_data, 0, sizeof(ua_data));
  ua_data.type = PWAOS_TYPE_USER_AGENT_INFO;
  ua_data.next = nullptr;

  PWAOS_API_CALL(env, pwaosApp_getUserAgent(app_handle, &ua_data));

  napi_value js_ua;
  NODE_API_CALL(env, napi_create_string_utf8(env, ua_data.userAgent, NAPI_AUTO_LENGTH, &js_ua));
  return js_ua;
}

static napi_value AppSetUserAgent(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: userAgent");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  napi_valuetype ua_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &ua_type));
  if (ua_type != napi_string) {
    napi_throw_type_error(env, nullptr, "Second argument must be a string");
    return nullptr;
  }

  size_t ua_len;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], nullptr, 0, &ua_len));
  if (ua_len >= MAX_URI_LEN) {
    napi_throw_error(env, nullptr, "UserAgent too long");
    return nullptr;
  }

  pwaosUserAgentInfo ua_data;
  memset(&ua_data, 0, sizeof(ua_data));
  ua_data.type = PWAOS_TYPE_USER_AGENT_INFO;
  ua_data.next = nullptr;
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], ua_data.userAgent, MAX_URI_LEN, &ua_len));

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_setUserAgent(app_handle, &ua_data));
}

static napi_value AppAddJavascriptInterface(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 2) {
    napi_throw_type_error(env, nullptr, "Expected 2 arguments: name, objInfHandle");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  // 获取接口名称
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
    napi_throw_error(env, nullptr, "Interface name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  // 获取对象接口句柄
  PWAOS_OBJECT_INTERFACE_HANDLE obj_handle = nullptr;
  napi_valuetype obj_type;
  NODE_API_CALL(env, napi_typeof(env, argv[1], &obj_type));
  if (obj_type == napi_external) {
    NODE_API_CALL(env, napi_get_value_external(env, argv[1], (void**)&obj_handle));
  } else if (obj_type == napi_number) {
    int64_t ptr_val;
    NODE_API_CALL(env, napi_get_value_int64(env, argv[1], &ptr_val));
    obj_handle = (PWAOS_OBJECT_INTERFACE_HANDLE)(uintptr_t)ptr_val;
  } else {
    napi_throw_type_error(env, nullptr, "Third argument must be an external or number handle");
    return nullptr;
  }

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_addJavascriptInterface(app_handle, name, obj_handle));
}

static napi_value AppRemoveJavascriptInterface(napi_env env, napi_callback_info info) {
  size_t argc = 1;
  napi_value argv[1];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected 1 arguments: name");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid pwa handle");
    return nullptr;
  }

  // 获取接口名称
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
    napi_throw_error(env, nullptr, "Interface name too long");
    return nullptr;
  }
  NODE_API_CALL(env, napi_get_value_string_utf8(env, argv[0], name, sizeof(name), &name_len));

  PWAOS_API_CALL_AND_RETURN(env, pwaosApp_removeJavascriptInterface(app_handle, name));
}

static napi_value AppSetCallback(napi_env env, napi_callback_info info) {
  size_t argc = 2;
  napi_value argv[2];
  napi_value this_arg;
  NODE_API_CALL(env, napi_get_cb_info(env, info, &argc, argv, &this_arg, nullptr));

  if (argc < 1) {
    napi_throw_type_error(env, nullptr, "Expected at least 1 argument: callback[, userData]");
    return nullptr;
  }

  // 从 this 对象中取出 app 句柄
  PWAOS_APP_HANDLE app_handle;
  napi_unwrap(env, this_arg, (void**)&app_handle);
  if (!app_handle) {
    napi_throw_error(env, nullptr, "Invalid app handle");
    return nullptr;
  }

  auto* app_status = GetAppStatus(app_handle);
  if (!app_status) {
    napi_throw_error(env, nullptr, "Internal error");
    return nullptr;
  }

  auto* last_callback_ctx = (AppCallbackContext*)app_status->userData;

  // 处理清除回调（callback 为 null/undefined）
  napi_valuetype cb_type;
  NODE_API_CALL(env, napi_typeof(env, argv[0], &cb_type));
  if (cb_type == napi_null || cb_type == napi_undefined) {
    napi_value ret;
    napi_create_int32(env, pwaosApp_setCallback(app_handle, nullptr, nullptr), &ret);
    if (last_callback_ctx) {
      app_status->userData = nullptr;
      last_callback_ctx->release(env);
      delete last_callback_ctx;
    }
    return ret;
  }

  // 必须是函数
  if (cb_type != napi_function) {
    napi_throw_type_error(env, nullptr, "First argument must be a function, null, or undefined");
    return nullptr;
  }

  // 获取 userData（可选）
  napi_value user_data;
  if (argc >= 2) {
    user_data = argv[1];
  } else {
    napi_get_undefined(env, &user_data);
  }

  auto ctx = std::make_unique<AppCallbackContext>();
  if (!ctx) {
    napi_throw_type_error(env, nullptr, "Out Of Memory!!!");
    return nullptr;
  }
  auto* raw_ctx = ctx.get();

  // 创建线程安全函数
  napi_value async_name;
  napi_create_string_utf8(env, "AppStatusCallback", NAPI_AUTO_LENGTH, &async_name);
  napi_create_threadsafe_function(env, argv[0], nullptr, async_name, 0, 1, nullptr,
                                  nullptr, raw_ctx, CallAppStatusCallback, &raw_ctx->tsfn);
  // 创建 userData 的持久引用
  napi_create_reference(env, user_data, 1, &raw_ctx->user_data_ref);

  // 注册 C 回调
  pwaosResult result = pwaosApp_setCallback(app_handle, AppCallback, raw_ctx);
  if (result != PWAOS_RESULT_SUCCESS) {
    // 失败则清理
    raw_ctx->release(env);
    throw_pwaos_error(env, result);
    return nullptr;
  }

  if (last_callback_ctx) {
    last_callback_ctx->release(env);
    delete last_callback_ctx;
    app_status->userData = nullptr;
  }

  app_status->userData = raw_ctx;
  ctx.release();

  napi_value ret;
  napi_create_int32(env, result, &ret);
  return ret;
}

static napi_value AppConstructor(napi_env env, napi_callback_info info) {
  return nullptr;
}

static napi_ref g_app_class_ref = nullptr;
napi_value InitAppClass(napi_env env, napi_value exports) {
  napi_property_descriptor methods[] = {
    DECLARE_NODE_API_PROPERTY("addJavascriptInterface", AppAddJavascriptInterface),
    DECLARE_NODE_API_PROPERTY("createProjector", AppCreateProjector),
    DECLARE_NODE_API_PROPERTY("destroyProjector", AppDestroyProjector),
    DECLARE_NODE_API_PROPERTY("getBounds", AppGetBounds),
    DECLARE_NODE_API_PROPERTY("executeJavaScript", AppExecuteJavaScript),
    DECLARE_NODE_API_PROPERTY("getDensity", AppGetDensity),
    DECLARE_NODE_API_PROPERTY("getDevToolsUri", AppGetDevToolsUri),
    DECLARE_NODE_API_PROPERTY("getUserAgent", AppGetUserAgent),
    DECLARE_NODE_API_PROPERTY("handleClick", AppHandleClick),
    DECLARE_NODE_API_PROPERTY("handlePointerInput", AppHandlePointerInput),
    DECLARE_NODE_API_PROPERTY("removeJavascriptInterface", AppRemoveJavascriptInterface),
    DECLARE_NODE_API_PROPERTY("setBounds", AppSetBounds),
    DECLARE_NODE_API_PROPERTY("setCallback", AppSetCallback),
    DECLARE_NODE_API_PROPERTY("setDensity", AppSetDensity),
    DECLARE_NODE_API_PROPERTY("setFPS", AppSetFPS),
    DECLARE_NODE_API_PROPERTY("setUserAgent", AppSetUserAgent),
  };

  napi_value app_class;
  NODE_API_CALL(
    env,
    napi_define_class(env, "pwaosApp", NAPI_AUTO_LENGTH,
                      AppConstructor, nullptr,
                      sizeof(methods) / sizeof(methods[0]),
                      methods, &app_class));

  NODE_API_CALL(env, napi_create_reference(env, app_class, 1, &g_app_class_ref));

  // pwaosAppType
  DefineStaticInt32Property(env, app_class, "appTypeUnknown", appTypeUnknown);
  DefineStaticInt32Property(env, app_class, "appTypePwa", appTypePwa);
  DefineStaticInt32Property(env, app_class, "appTypeBrowser", appTypeBrowser);
  DefineStaticInt32Property(env, app_class, "appTypeConsole", appTypeConsole);
  DefineStaticInt32Property(env, app_class, "appTypeDesktop", appTypeDesktop);
  DefineStaticInt32Property(env, app_class, "appTypePwaContents", appTypePwaContents);

  // pwaosAppStatus
  DefineStaticInt32Property(env, app_class, "appStatusInVisible", appStatusInVisible);
  DefineStaticInt32Property(env, app_class, "appStatusStopping", appStatusStopping);
  DefineStaticInt32Property(env, app_class, "appStatusReady", appStatusReady);
  DefineStaticInt32Property(env, app_class, "appStatusSynchronized", appStatusSynchronized);
  DefineStaticInt32Property(env, app_class, "appStatusVisible", appStatusVisible);
  DefineStaticInt32Property(env, app_class, "appStatusFocused", appStatusFocused);
  DefineStaticInt32Property(env, app_class, "appStatusUnFocused", appStatusUnFocused);

  return app_class;
}

napi_value GetOrCreateAppObj(napi_env env, PWAOS_APP_HANDLE app_handle) {
  const auto& iter = g_apps.find(app_handle);
  if (iter != g_apps.end()) {
    napi_value obj;
    NODE_API_CALL(env, napi_get_reference_value(env, iter->second, &obj));
    return obj;
  }

  napi_value app_class;
  NODE_API_CALL(env, napi_get_reference_value(env, g_app_class_ref, &app_class));

  napi_value pwa_obj;
  NODE_API_CALL(env, napi_new_instance(env, app_class, 0, 0, &pwa_obj));
  napi_wrap(env, pwa_obj, app_handle, app_obj_finalizer, nullptr, nullptr);

  napi_ref ref;
  NODE_API_CALL(env, napi_create_reference(env, pwa_obj, 0, &ref));

  g_apps.emplace(app_handle, ref);

  CreateAppStatus(app_handle);
  return pwa_obj;
}

void ReleaseAllApps(napi_env env) {
  for (auto& [instance, ref] : g_apps)
    napi_delete_reference(env, ref);
  g_apps.clear();

  for (auto& [instance, app_status] : g_apps_status)
  if (app_status.userData) {
    auto* callback_ctx = (AppCallbackContext*)app_status.userData;
    callback_ctx->release(env);
    delete callback_ctx;
  }
  g_apps_status.clear();
}