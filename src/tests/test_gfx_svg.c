#include "tests_common.h"

TEST(test_gfx_svg_group_from_line) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-fx.svg) "
      "  (def gsvg (tiny-fx.svg/group-from-svg "
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

TEST(test_gfx_svg_polygon_maps_to_closed_polyline) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-fx.svg) "
      "  (def gsvg_poly (tiny-fx.svg/group-from-svg "
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

TEST(test_gfx_svg_circle_maps_to_polygon_polyline) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-fx.svg) "
      "  (def gsvg_circle (tiny-fx.svg/group-from-svg "
      "                    \"<svg><circle cx='20' cy='30' r='8' fill='#FF0000'/></svg>\")) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, ok);

  ID circle_ok = eval_string(
      "(let [node (first (get gsvg_circle :children)) "
      "      pts (:pts node) "
      "      style (:style node)] "
      "  (and (= true (:closed node)) "
      "       (= 16 (count pts)) "
      "       (= [20 22] (first pts)) "
      "       (= [28 30] (nth pts 4)) "
      "       (= [20 38] (nth pts 8)) "
      "       (= [12 30] (nth pts 12)) "
      "       (= true (:has_fill style))))",
      g_test_eval_state);
  TEST_ASSERT_TRUE(circle_ok && circle_ok != clj_false);
}

TEST(test_gfx_svg_text_maps_to_vtext) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-fx.svg) "
      "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
      "  (def gsvg_text (tiny-fx.svg/group-from-svg "
      "                  \"<svg><text x='12' y='34' fill='#00FF00' font-size='16'>TinyBoy</text></svg>\")) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, ok);

  ID text_ok = eval_string(
      "(let [node (first (get gsvg_text :children)) "
      "      style (get node :style)] "
      "  (and (= \"TinyBoy\" (get node :text)) "
      "       (= 12 (get node :x)) "
      "       (= 34 (get node :y)) "
      "       (= 2 (get node :scale)) "
      "       (= (tiny-fx.color/web-hex->color \"#00FF00\") "
      "          (get style :stroke-color))))",
      g_test_eval_state);
  TEST_ASSERT_TRUE(text_ok && text_ok != clj_false);
}

TEST(test_gfx_svg_text_decodes_entities_and_inline_style) {
  ID ok = eval_string(
      "(do "
      "  (require 'tiny-fx.svg) "
      "  (require 'tiny-fx.gfx-scene) "
        "  (require 'tiny-fx.color) "
      "  (def gsvg_text_style (tiny-fx.svg/group-from-svg "
      "                        \"<svg><text x='5' y='9' style='fill:#FF00FF;font-size:8px'>A&amp;B &lt;3</text></svg>\")) "
      "  true)",
      g_test_eval_state);
  TEST_ASSERT_EQUAL_PTR(clj_true, ok);

  ID text_ok = eval_string(
      "(let [node (first (get gsvg_text_style :children)) "
      "      style (get node :style)] "
      "  (and (= \"A&B <3\" (get node :text)) "
      "       (= 1 (get node :scale)) "
      "       (= (tiny-fx.color/web-hex->color \"#FF00FF\") "
      "          (get style :stroke-color))))",
      g_test_eval_state);
  TEST_ASSERT_TRUE(text_ok && text_ok != clj_false);
}
