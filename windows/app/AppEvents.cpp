#include "app/AppEvents.h"

namespace wp {

AppEvents &AppEvents::instance()
{
    static AppEvents events;
    return events;
}

} // namespace wp
