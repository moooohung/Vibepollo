#include "src/platform/windows/display_helper_borrow_gate.h"

#include <gtest/gtest.h>
#include <thread>

bool borrow_gate_entered_from_other_translation_unit(const display_helper::borrow::gate_t &gate);
display_helper::borrow::gate_t::scope_t borrow_gate_enter_from_other_translation_unit(display_helper::borrow::gate_t &gate);

TEST(DisplayHelperBorrowTls, ReentrancyIsSharedAcrossTranslationUnits) {
  display_helper::borrow::gate_t gate;
  EXPECT_FALSE(borrow_gate_entered_from_other_translation_unit(gate));
  auto outer = gate.try_enter();
  ASSERT_TRUE(outer);
  EXPECT_TRUE(borrow_gate_entered_from_other_translation_unit(gate));
  auto inner = borrow_gate_enter_from_other_translation_unit(gate);
  ASSERT_TRUE(inner);
  EXPECT_EQ(gate.active(), 1u);
  outer.reset();
  EXPECT_TRUE(gate.entered_on_this_thread());
  inner.reset();
  EXPECT_FALSE(gate.entered_on_this_thread());
  EXPECT_EQ(gate.active(), 0u);
}

TEST(DisplayHelperBorrowTls, ReentrancyDoesNotLeakBetweenThreads) {
  display_helper::borrow::gate_t gate;
  auto outer = gate.try_enter();
  ASSERT_TRUE(outer);
  std::thread other([&] {
    EXPECT_FALSE(borrow_gate_entered_from_other_translation_unit(gate));
    auto inner = borrow_gate_enter_from_other_translation_unit(gate);
    EXPECT_TRUE(inner);
    EXPECT_EQ(gate.active(), 2u);
  });
  other.join();
  EXPECT_EQ(gate.active(), 1u);
}
