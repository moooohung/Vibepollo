#include "src/platform/windows/display_helper_borrow_gate.h"

bool borrow_gate_entered_from_other_translation_unit(const display_helper::borrow::gate_t &gate) {
  return gate.entered_on_this_thread();
}

display_helper::borrow::gate_t::scope_t borrow_gate_enter_from_other_translation_unit(display_helper::borrow::gate_t &gate) {
  return gate.try_enter();
}
