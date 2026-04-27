#pragma once

#include "ITheme.h"

#include <QApplication>
#include <QPalette>
#include <QColor>

class FusionTheme : public ITheme
{
public:
    virtual ~FusionTheme() {}

    QString qtTheme() override
    {
        return "Fusion";
    }

    void apply(bool initial) override
    {
        qApp->setStyle("Fusion");
        ITheme::apply(initial);
    }

    double fadeAmount()
    {
        return 0.5;
    }

    QColor fadeColor()
    {
        return QColor(49, 54, 59);
    }

protected:
    static QPalette fadeInactive(QPalette palette, double fadeAmount, QColor fadeColor)
    {
        QColor text = palette.color(QPalette::WindowText);
        text.setAlpha(255);
        QColor disabledText = palette.color(QPalette::WindowText);
        disabledText.setAlpha(static_cast<int>(255 * fadeAmount));

        QColor base = palette.color(QPalette::Base);
        QColor disabledBase = blendColors(base, fadeColor, fadeAmount);

        QColor window = palette.color(QPalette::Window);
        QColor disabledWindow = blendColors(window, fadeColor, fadeAmount);

        QColor highlight = palette.color(QPalette::Highlight);
        QColor disabledHighlight = blendColors(highlight, fadeColor, fadeAmount);

        palette.setColor(QPalette::Disabled, QPalette::WindowText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Text, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::ButtonText, disabledText);
        palette.setColor(QPalette::Disabled, QPalette::Base, disabledBase);
        palette.setColor(QPalette::Disabled, QPalette::AlternateBase, disabledBase);
        palette.setColor(QPalette::Disabled, QPalette::Window, disabledWindow);
        palette.setColor(QPalette::Disabled, QPalette::Button, disabledWindow);
        palette.setColor(QPalette::Disabled, QPalette::Highlight, disabledHighlight);

        return palette;
    }

    static QColor blendColors(QColor color1, QColor color2, double factor)
    {
        int r = color1.red() + static_cast<int>((color2.red() - color1.red()) * factor);
        int g = color1.green() + static_cast<int>((color2.green() - color1.green()) * factor);
        int b = color1.blue() + static_cast<int>((color2.blue() - color1.blue()) * factor);
        return QColor(r, g, b);
    }
};
