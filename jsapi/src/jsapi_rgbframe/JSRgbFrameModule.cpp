#include "JSRgbFramePlayer.h"

#include "jqutil_v2/jqutil.h"
#include "jsmodules/JSCModuleExtension.h"
#include "jquick_config.h"

using namespace JQUTIL_NS;

namespace rgbframe {

static std::vector<std::string> exportList = {
    "rgbFramePlayer"
};

static int module_init(JSContext *ctx, JSModuleDef *m)
{
    JQuick::sp<JQModuleEnv> env = JQModuleEnv::CreateModule(ctx, m, "rgbframe");
    rgbframe_init(env.get());
    env->setModuleExportDone(JS_UNDEFINED, exportList);
    return 0;
}

DEF_MODULE_LOAD_FUNC_EXPORT(rgbframe, module_init, exportList)

}  // namespace rgbframe

extern "C" JQUICK_EXPORT void custom_init_jsapis()
{
    registerCModuleLoader("rgbframe", &rgbframe::rgbframe_module_load);
}
