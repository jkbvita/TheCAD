#include "presentation/ViewportAppearance.h"
namespace thecad::presentation
{
void applyViewportAppearance(const Handle(V3d_View) & view, bool dark)
{
    if (view.IsNull())
        return;
    const Quantity_Color top = dark ? Quantity_Color(0.12549, 0.15686, 0.20, Quantity_TOC_sRGB)
                                    : Quantity_Color(0.90, 0.92, 0.95, Quantity_TOC_RGB);
    const Quantity_Color bottom = dark
                                      ? Quantity_Color(0.20784, 0.26275, 0.32549, Quantity_TOC_sRGB)
                                      : Quantity_Color(0.99, 0.99, 0.99, Quantity_TOC_RGB);
    view->SetBgGradientColors(top, bottom, Aspect_GFM_VER, Standard_False);
    view->TriedronDisplay(Aspect_TOTP_LEFT_LOWER, dark ? Quantity_NOC_GRAY80 : Quantity_NOC_GRAY30,
                          0.08, V3d_ZBUFFER);
}
} // namespace thecad::presentation
