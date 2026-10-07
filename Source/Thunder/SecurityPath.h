#pragma once

namespace Thunder {
namespace PluginHost {
namespace Detail {

// PUBLIC_INTERFACE
template <typename STRING>
bool IsControllerPath(const STRING& path, const STRING& controllerPath)
{
    /** Return whether path names the controller or a slash-delimited child.
     * Both arguments use the same string type. Similar prefixes are rejected.
     */
    return (path.compare(0, controllerPath.size(), controllerPath) == 0)
        && ((path.size() == controllerPath.size())
            || (path[controllerPath.size()] == '/'));
}

}
}
}
