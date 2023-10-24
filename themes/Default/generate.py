"""
Script used to regenerate files for the default theme.

You can copy and paste this script to use as the starting point for creating
your own theme, or edit the theme files directly.
"""
import cairo
import math
from dataclasses import dataclass

border = 1
radius = 8
size = 32

focused_colour = (0.154, 0.308, 0.42, 1.0)
unfocused_colour = (0.6, 0.6, 0.6, 1.0)
urgent_colour = (0.5, 0.1, 0.1, 1.0)
border_outer_colour = (0.11, 0.11, 0.11, 1.0)
background_colour = (0.6, 0.6, 0.6, 1.0)


@dataclass
class Colours:
    foreground: tuple[float, float, float, float]
    background_titlebar: tuple[float, float, float, float]
    background_content: tuple[float, float, float, float]
    border_outer: tuple[float, float, float, float]
    border_highlight: tuple[float, float, float, float]
    border_inner: tuple[float, float, float, float]


colours_focused = Colours(
    foreground=(1.0, 1.0, 1.0, 1.0),
    background_titlebar=focused_colour,
    background_content=background_colour,
    border_outer=border_outer_colour,
    border_highlight=focused_colour,
    border_inner=(0.15, 0.15, 0.15, 1.0),
)


colours_active = Colours(
    foreground=(1.0, 1.0, 1.0, 1.0),
    background_titlebar=unfocused_colour,
    background_content=background_colour,
    border_outer=border_outer_colour,
    border_highlight=unfocused_colour,
    border_inner=(0.15, 0.15, 0.15, 1.0),
)


colours_inactive = Colours(
    foreground=(1.0, 1.0, 1.0, 1.0),
    background_titlebar=unfocused_colour,
    background_content=background_colour,
    border_outer=border_outer_colour,
    border_highlight=unfocused_colour,
    border_inner=(0.15, 0.15, 0.15, 1.0),
)


colours_urgent = Colours(
    foreground=(1.0, 1.0, 1.0, 1.0),
    background_titlebar=urgent_colour,
    background_content=background_colour,
    border_outer=border_outer_colour,
    border_highlight=urgent_colour,
    border_inner=(0.15, 0.15, 0.15, 1.0),
)


def _lighten(s, r, g, b, a):
    assert s >= 0
    assert s <= 1
    return (
        1 - (1 - s) * (1 - r),
        1 - (1 - s) * (1 - g),
        1 - (1 - s) * (1 - b),
        a,
    )


def _darken(s, r, g, b, a):
    assert s >= 0
    assert s <= 1
    return (
        (1 - s) * r,
        (1 - s) * g,
        (1 - s) * b,
        a,
    )


def _fill_titlebar(cr, colours):
    cr.save()
    fill = cairo.LinearGradient(0, 0, 0, size)
    fill.add_color_stop_rgba(0, *_lighten(0.2, *colours.background_titlebar))
    fill.add_color_stop_rgba(0.5 * radius / size, *colours.background_titlebar)
    fill.add_color_stop_rgba(1, *_darken(0.1, *colours.background_titlebar))
    cr.set_source(fill)
    cr.fill()
    cr.restore()


def _stroke_border_outer(cr, colours):
    cr.save()
    cr.set_line_width(border)
    cr.set_source_rgba(*colours.border_outer)
    cr.stroke()
    cr.restore()


def _fill_border_highlight(cr, colours):
    cr.save()
    cr.set_source_rgba(*colours.border_highlight)
    cr.fill()
    cr.restore()


def _fill_background_content(cr, colours):
    cr.save()
    cr.set_source_rgba(*colours.background_content)
    cr.fill()
    cr.restore()


def _stroke_border_inner(cr, colours):
    cr.save()
    cr.set_line_width(border)
    cr.set_source_rgba(*colours.border_inner)
    cr.stroke()
    cr.restore()


def _outline_titlebar_floating(cr, /):
    cr.move_to(0.5 * border, size - 0.5 * border)
    cr.line_to(0.5 * border, radius)
    cr.arc(radius, radius, radius - 0.5 * border, -math.pi, -math.pi / 2)
    cr.line_to(size - radius, 0.5 * border)
    cr.arc(size - radius, radius, radius - 0.5 * border, -math.pi / 2, 0)
    cr.line_to(size - 0.5 * border, size - 0.5 * border)
    cr.close_path()


def _outline_outer_border_floating(cr, /):
    cr.move_to(0.5 * border, 0)
    cr.line_to(0.5 * border, size - 0.5 * border)
    cr.line_to(size - 0.5 * border, size - 0.5 * border)
    cr.line_to(size - 0.5 * border, 0)


def _outline_inner_border_floating(cr, /):
    cr.move_to(2.5 * border, 0)
    cr.line_to(2.5 * border, size - 3.5 * border)
    cr.line_to(size - 2.5 * border, size - 3.5 * border)
    cr.line_to(size - 2.5 * border, 0)


def _gen_floating_titlebar(name, colours):
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        _outline_titlebar_floating(cr)
        _fill_titlebar(cr, colours)

        _outline_titlebar_floating(cr)
        _stroke_border_outer(cr, colours)

        surface.write_to_png(name)


def _gen_floating_border(name, colours):
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        _outline_outer_border_floating(cr)
        _fill_border_highlight(cr, colours)

        _outline_outer_border_floating(cr)
        _stroke_border_outer(cr, colours)

        _outline_inner_border_floating(cr)
        _fill_background_content(cr, colours)

        _outline_inner_border_floating(cr)
        _stroke_border_inner(cr, colours)

        surface.write_to_png(name)


"""
Rules:
  - Top of titlebar only depends on what came before.
  - Bottom of titlebar depends on if shaded or not (but in practice is the same)
"""


def _gen_tiled_head_titlebar(name, colours):
    # Top border and bottom border.
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        cr.move_to(0, 0.5 * border)
        cr.line_to(size, 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        cr.line_to(0, size - 0.5 * border)
        cr.close_path()
        _fill_titlebar(cr, colours)

        cr.move_to(0, 0.5 * border)
        cr.line_to(size, 0.5 * border)
        cr.move_to(0, size - 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        _stroke_border_outer(cr, colours)

        surface.write_to_png(name)


def _gen_tiled_head_shaded_titlebar(name, colours):
    # Top and bottom border.
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        cr.move_to(0, 0.5 * border)
        cr.line_to(size, 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        cr.line_to(0, size - 0.5 * border)
        cr.close_path()
        _fill_titlebar(cr, colours)

        cr.move_to(0, 0.5 * border)
        cr.line_to(size, 0.5 * border)
        cr.move_to(0, size - 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        _stroke_border_outer(cr, colours)

        surface.write_to_png(name)


def _gen_tiled_titlebar(name, colours):
    # Bottom border.
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        cr.move_to(0, 0)
        cr.line_to(size, 0)
        cr.line_to(size, size - 0.5 * border)
        cr.line_to(0, size - 0.5 * border)
        cr.close_path()
        _fill_titlebar(cr, colours)

        cr.move_to(0, size - 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        _stroke_border_outer(cr, colours)

        surface.write_to_png(name)


def _gen_tiled_shaded_titlebar(name, colours):
    # Bottom border
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        cr.move_to(0, 0)
        cr.line_to(size, 0)
        cr.line_to(size, size - 0.5 * border)
        cr.line_to(0, size - 0.5 * border)
        cr.close_path()
        _fill_titlebar(cr, colours)

        cr.move_to(0, size - 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        _stroke_border_outer(cr, colours)

        surface.write_to_png(name)


def _gen_tiled_border(name, colours):
    # Bottom border and full inner border
    with cairo.ImageSurface(cairo.Format.ARGB32, size, size) as surface:
        cr = cairo.Context(surface)

        cr.move_to(0, 0)
        cr.line_to(size, 0)
        cr.line_to(size, size - 0.5 * border)
        cr.line_to(0, size - 0.5 * border)
        cr.close_path()
        _fill_border_highlight(cr, colours)

        cr.move_to(0, size - 0.5 * border)
        cr.line_to(size, size - 0.5 * border)
        _stroke_border_outer(cr, colours)

        cr.move_to(1.5 * border, 0)
        cr.line_to(1.5 * border, size - 2.5 * border)
        cr.line_to(size - 1.5 * border, size - 2.5 * border)
        cr.line_to(size - 1.5 * border, 0)
        _fill_background_content(cr, colours)

        cr.move_to(1.5 * border, 0)
        cr.line_to(1.5 * border, size - 2.5 * border)
        cr.line_to(size - 1.5 * border, size - 2.5 * border)
        cr.line_to(size - 1.5 * border, 0)
        _stroke_border_inner(cr, colours)

        surface.write_to_png(name)


if __name__ == "__main__":
    _gen_floating_titlebar("floating_focused_titlebar.png", colours_focused)
    _gen_floating_titlebar("floating_active_titlebar.png", colours_active)
    _gen_floating_titlebar("floating_inactive_titlebar.png", colours_inactive)
    _gen_floating_titlebar("floating_urgent_titlebar.png", colours_urgent)
    _gen_floating_border("floating_focused_border.png", colours_focused)
    _gen_floating_border("floating_active_border.png", colours_active)
    _gen_floating_border("floating_inactive_border.png", colours_inactive)
    _gen_floating_border("floating_urgent_border.png", colours_urgent)
    _gen_tiled_head_titlebar("tiled-head_focused_titlebar.png", colours_focused)
    _gen_tiled_head_titlebar("tiled-head_active_titlebar.png", colours_active)
    _gen_tiled_head_titlebar("tiled-head_inactive_titlebar.png", colours_inactive)
    _gen_tiled_head_titlebar("tiled-head_urgent_titlebar.png", colours_urgent)
    _gen_tiled_head_shaded_titlebar(
        "tiled-head-shaded_focused_titlebar.png", colours_focused
    )
    _gen_tiled_head_shaded_titlebar(
        "tiled-head-shaded_active_titlebar.png", colours_active
    )
    _gen_tiled_head_shaded_titlebar(
        "tiled-head-shaded_inactive_titlebar.png", colours_inactive
    )
    _gen_tiled_head_shaded_titlebar(
        "tiled-head-shaded_urgent_titlebar.png", colours_urgent
    )
    _gen_tiled_titlebar("tiled_focused_titlebar.png", colours_focused)
    _gen_tiled_titlebar("tiled_active_titlebar.png", colours_active)
    _gen_tiled_titlebar("tiled_inactive_titlebar.png", colours_inactive)
    _gen_tiled_titlebar("tiled_urgent_titlebar.png", colours_urgent)
    _gen_tiled_shaded_titlebar("tiled-shaded_focused_titlebar.png", colours_focused)
    _gen_tiled_shaded_titlebar("tiled-shaded_active_titlebar.png", colours_active)
    _gen_tiled_shaded_titlebar("tiled-shaded_inactive_titlebar.png", colours_inactive)
    _gen_tiled_shaded_titlebar("tiled-shaded_urgent_titlebar.png", colours_urgent)
    _gen_tiled_border("tiled_focused_border.png", colours_focused)
    _gen_tiled_border("tiled_active_border.png", colours_focused)
    _gen_tiled_border("tiled_inactive_border.png", colours_focused)
    _gen_tiled_border("tiled_urgent_border.png", colours_focused)
