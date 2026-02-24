#include "tests_common.h"

TEST(test_tiny_drw_svg_group_from_line) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-drw.svg) "
      "  (def gsvg (tiny-drw.svg/group-from-svg "
      "             \"<svg><line x1='1' y1='2' x2='30' y2='40' stroke='#FF0000' stroke-width='2'/></svg>\")) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, ok);

  ID child_count = eval_string("(count (get gsvg :children))", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum(child_count));
  TEST_ASSERT_EQUAL_INT(1, as_fixnum(child_count));

  ID x1 = eval_string("(get (first (get gsvg :children)) :x1)", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum(x1));
  TEST_ASSERT_EQUAL_INT(1, as_fixnum(x1));

  ID y2 = eval_string("(get (first (get gsvg :children)) :y2)", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum(y2));
  TEST_ASSERT_EQUAL_INT(40, as_fixnum(y2));

  ID stroke_width = eval_string("(get (get (first (get gsvg :children)) :style) :stroke_width)", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum(stroke_width));
  TEST_ASSERT_EQUAL_INT(2, as_fixnum(stroke_width));
}

TEST(test_tiny_drw_svg_polygon_maps_to_closed_polyline) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-drw.svg) "
      "  (def gsvg_poly (tiny-drw.svg/group-from-svg "
      "                  \"<svg><polygon points='0,0 10,0 10,10 0,10' fill='#00FF00'/></svg>\")) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, ok);

  ID closed = eval_string("(get (first (get gsvg_poly :children)) :closed)", g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, closed);

  ID pts_count = eval_string("(count (get (first (get gsvg_poly :children)) :pts))", g_test_eval_state);
  TEST_ASSERT_TRUE(is_fixnum(pts_count));
  TEST_ASSERT_EQUAL_INT(4, as_fixnum(pts_count));
}
