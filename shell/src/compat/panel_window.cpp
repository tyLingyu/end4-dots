#include "compat/panel_window.h"

namespace ii::qs {

  // The layer surface (and rendering contentItem into it) comes in stage 3c.
  PanelWindow::PanelWindow() { m_contentItem = create<Item>(); }

  PanelWindow::~PanelWindow() = default;

} // namespace ii::qs
