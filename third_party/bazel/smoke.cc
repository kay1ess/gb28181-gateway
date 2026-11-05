#include <drogon/drogon.h>
#include <mk_util.h>
#include <resip/dum/DialogUsageManager.hxx>
#include <resip/stack/SipStack.hxx>

int main() {
  resip::SipStack stack;
  resip::DialogUsageManager dum(stack);
  (void)drogon::app();
  (void)mk_util_get_current_millisecond();
  return 0;
}
