#include "SwTimer.hpp"
#include "System.hpp"

/**
  * @brief  Get the current millisecond counter value
  * @param  none
  * @retval amount of milliseconds
  */
uint32_t Driver::SwTimer::getTicks(void)
{
    return Driver::System::GetTimeMs();
}
