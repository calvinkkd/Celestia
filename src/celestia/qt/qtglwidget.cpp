/***************************************************************************
                          kdeglwidget.cpp  -  description
                             -------------------
    begin                : Tue Jul 16 2002
    copyright            : (C) 2002 by Christophe Teyssier
    email                : chris@teyssier.org
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/

#include <cassert>
#include <celengine/celestia.h>
#include <celengine/starbrowser.h>

#include <QCursor>
#include <QPaintDevice>
#include <QMouseEvent>
#include <QSettings>

#ifndef DEBUG
#  define G_DISABLE_ASSERT
#endif

#include "celengine/astro.h"
#include "celutil/util.h"
#include "celutil/filetype.h"
#include "celutil/debug.h"
#include "celestia/imagecapture.h"
#include "celestia/celestiacore.h"
#include "celengine/simulation.h"
#include "celengine/glcontext.h"

#include "qtglwidget.h"

#include <cmath>
#include <vector>

using namespace Qt;


const int DEFAULT_ORBIT_MASK = Body::Planet | Body::Moon | Body::Stellar;

const int DEFAULT_LABEL_MODE = 2176;

const float DEFAULT_AMBIENT_LIGHT_LEVEL = 0.1f;

const int DEFAULT_STARS_COLOR = ColorTable_Blackbody_D65;

const float DEFAULT_VISUAL_MAGNITUDE = 8.0f;

const Renderer::StarStyle DEFAULT_STAR_STYLE = Renderer::FuzzyPointStars;

const unsigned int DEFAULT_TEXTURE_RESOLUTION = medres;


CelestiaGlWidget::CelestiaGlWidget(QWidget* parent, const char* /* name */, CelestiaCore* core) :
    QGLWidget(parent)
{
    setFocusPolicy(Qt::ClickFocus);

    appCore = core;
    appRenderer = appCore->getRenderer();
    appSim = appCore->getSimulation();

    setCursor(QCursor(Qt::CrossCursor));
    currentCursor = CelestiaCore::CrossCursor;
    setMouseTracking(true);

    cursorVisible = true;
}


/*!
  Paint the box. The actual openGL commands for drawing the box are
  performed here.
*/

void CelestiaGlWidget::paintGL()
{
    appCore->draw();
}


static GLContext::GLRenderPath getBestAvailableRenderPath(const GLContext& /*glc*/)
{
#if 0
    const GLContext::GLRenderPath renderPaths[] = {
        GLContext::GLPath_GLSL,
    };

    for (auto renderPath : renderPaths)
    {
        if (glc.renderPathSupported(renderPath))
            return renderPath;
    }
#endif

    return GLContext::GLPath_GLSL;
}


/*!
  Set up the OpenGL rendering state, and define display list
*/

void CelestiaGlWidget::initializeGL()
{
    if (!appCore->initRenderer())
    {
        // cerr << "Failed to initialize renderer.\n";
        exit(1);
    }

    appCore->tick();

    // Read saved settings
    QSettings settings;
    appRenderer->setRenderFlags(settings.value("RenderFlags", static_cast<quint64>(Renderer::DefaultRenderFlags)).toULongLong());
    appRenderer->setOrbitMask(settings.value("OrbitMask", DEFAULT_ORBIT_MASK).toInt());
    appRenderer->setLabelMode(settings.value("LabelMode", DEFAULT_LABEL_MODE).toInt());
    appRenderer->setAmbientLightLevel((float) settings.value("AmbientLightLevel", DEFAULT_AMBIENT_LIGHT_LEVEL).toDouble());
    appRenderer->setStarStyle((Renderer::StarStyle) settings.value("StarStyle", DEFAULT_STAR_STYLE).toInt());
    appRenderer->setResolution(settings.value("TextureResolution", DEFAULT_TEXTURE_RESOLUTION).toUInt());

    if (settings.value("StarsColor", DEFAULT_STARS_COLOR).toInt() == 0)
        appRenderer->setStarColorTable(GetStarColorTable(ColorTable_Enhanced));
    else
        appRenderer->setStarColorTable(GetStarColorTable(ColorTable_Blackbody_D65));

    appCore->getSimulation()->setFaintestVisible((float) settings.value("Preferences/VisualMagnitude", DEFAULT_VISUAL_MAGNITUDE).toDouble());

    // Read the saved render path
    GLContext::GLRenderPath bestPath = getBestAvailableRenderPath(*appRenderer->getGLContext());
    GLContext::GLRenderPath savedPath = (GLContext::GLRenderPath) settings.value("RenderPath", bestPath).toInt();

    // Use the saved path only if it's supported (otherwise a graphics card
    // downgrade could cause Celestia to not function.)
    GLContext::GLRenderPath usePath;
    if (appRenderer->getGLContext()->renderPathSupported(savedPath))
        usePath = savedPath;
    else
        usePath = bestPath;

    appRenderer->getGLContext()->setRenderPath(usePath);

    appCore->setScreenDpi(logicalDpiY());
}


void CelestiaGlWidget::resizeGL(int w, int h)
{
    appCore->resize(w, h);
}


void CelestiaGlWidget::mouseMoveEvent(QMouseEvent* m)
{
    int x = (int) m->x();
    int y = (int) m->y();

    int buttons = 0;
    if (m->buttons() & LeftButton)
        buttons |= CelestiaCore::LeftButton;
    if (m->buttons() & MidButton)
        buttons |= CelestiaCore::MiddleButton;
    if (m->buttons() & RightButton)
        buttons |= CelestiaCore::RightButton;
    if (m->modifiers() & ShiftModifier)
        buttons |= CelestiaCore::ShiftKey;
    if (m->modifiers() & ControlModifier)
        buttons |= CelestiaCore::ControlKey;

#ifdef TARGET_OS_MAC
    // On the Mac, right dragging is be simulated with Option+left drag.
    // We may want to enable this on other platforms, though it's mostly only helpful
    // for users with single button mice.
    if (m->modifiers() & AltModifier)
    {
        buttons &= ~CelestiaCore::LeftButton;
        buttons |= CelestiaCore::RightButton;
    }
#endif

    if ((m->buttons() & (LeftButton | RightButton)) != 0)
    {
        if (cursorVisible)
        {
            // Hide the cursor.
            setCursor(QCursor(Qt::BlankCursor));
            cursorVisible = false;

            // Save the cursor position
            QPoint pt;
            pt.setX(x);
            pt.setY(y);

            // Store a local and global location
            saveLocalCursorPos = pt;
            pt = mapToGlobal(pt);
            saveGlobalCursorPos = pt;
        }

        // Calculate mouse delta from local coordinate then move it back to the saved location
        appCore->mouseMove(x - saveLocalCursorPos.rx(), y - saveLocalCursorPos.ry(), buttons);
        QCursor::setPos(saveGlobalCursorPos);
    }
    else
    {
        appCore->mouseMove(x, y);
    }
}


void CelestiaGlWidget::mousePressEvent( QMouseEvent* m )
{
    if (m->button() == LeftButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::LeftButton);
    else if (m->button() == MidButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::MiddleButton);
    else if (m->button() == RightButton)
        appCore->mouseButtonDown(m->x(), m->y(), CelestiaCore::RightButton);
}


void CelestiaGlWidget::mouseReleaseEvent( QMouseEvent* m )
{
    if (m->button() == LeftButton)
    {
        if (!cursorVisible)
        {
            // Restore the cursor position and make it visible again.
            setCursor(QCursor(Qt::CrossCursor));
            cursorVisible = true;
            QCursor::setPos(saveGlobalCursorPos);
        }
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::LeftButton);
    }
    else if (m->button() == MidButton)
    {
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::MiddleButton);
    }
    else if (m->button() == RightButton)
    {
        if (!cursorVisible)
        {
            // Restore the cursor position and make it visible again.
            setCursor(QCursor(Qt::CrossCursor));
            cursorVisible = true;
            QCursor::setPos(saveGlobalCursorPos);
        }
        appCore->mouseButtonUp(m->x(), m->y(), CelestiaCore::RightButton);
    }
}


void CelestiaGlWidget::wheelEvent( QWheelEvent* w )
{
    if (w->delta() > 0 )
    {
        appCore->mouseWheel(-1.0f, 0);
    }
    else if (w->delta() < 0)
    {
        appCore->mouseWheel(1.0f, 0);
    }
}


bool CelestiaGlWidget::handleSpecialKey(QKeyEvent* e, bool down)
{
    int k = -1;
    switch (e->key())
    {
    case Key_Up:
        k = CelestiaCore::Key_Up;
        break;
    case Key_Down:
        k = CelestiaCore::Key_Down;
        break;
    case Key_Left:
        k = CelestiaCore::Key_Left;
        break;
    case Key_Right:
        k = CelestiaCore::Key_Right;
        break;
    case Key_Home:
        k = CelestiaCore::Key_Home;
        break;
    case Key_End:
        k = CelestiaCore::Key_End;
        break;
    case Key_F1:
        k = CelestiaCore::Key_F1;
        break;
    case Key_F2:
        k = CelestiaCore::Key_F2;
        break;
    case Key_F3:
        k = CelestiaCore::Key_F3;
        break;
    case Key_F4:
        k = CelestiaCore::Key_F4;
        break;
    case Key_F5:
        k = CelestiaCore::Key_F5;
        break;
    case Key_F6:
        k = CelestiaCore::Key_F6;
        break;
    case Key_F7:
        k = CelestiaCore::Key_F7;
        break;
    case Key_F11:
        k = CelestiaCore::Key_F11;
        break;
    case Key_F12:
        k = CelestiaCore::Key_F12;
        break;
    case Key_PageDown:
        k = CelestiaCore::Key_PageDown;
        break;
    case Key_PageUp:
        k = CelestiaCore::Key_PageUp;
        break;
/*    case Key_F10:
        if (e->modifiers()& ShiftModifier)
            k = CelestiaCore::Key_F10;
        break;*/
    case Key_0:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad0;
        break;
    case Key_1:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad1;
        break;
    case Key_2:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad2;
        break;
    case Key_3:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad3;
        break;
    case Key_4:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad4;
        break;
    case Key_5:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad5;
        break;
    case Key_6:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad6;
        break;
    case Key_7:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad7;
        break;
    case Key_8:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad8;
        break;
    case Key_9:
        if (e->modifiers() & Qt::KeypadModifier)
            k = CelestiaCore::Key_NumPad9;
        break;
    case Qt::Key_A:
        if (e->modifiers() == NoModifier)
            k = 'A';
        break;
    case Qt::Key_Z:
        if (e->modifiers() == NoModifier)
            k = 'Z';
        break;
    }

    if (k >= 0)
    {
        int buttons = 0;
        if (e->modifiers() & ShiftModifier)
            buttons |= CelestiaCore::ShiftKey;

        if (down)
            appCore->keyDown(k, buttons);
        else
            appCore->keyUp(k);
        return (k < 'A' || k > 'Z');
    }

    return false;
}


void CelestiaGlWidget::keyPressEvent( QKeyEvent* e )
{
    int modifiers = 0;
    if (e->modifiers() & ShiftModifier)
        modifiers |= CelestiaCore::ShiftKey;
    if (e->modifiers() & ControlModifier)
        modifiers |= CelestiaCore::ControlKey;

    switch (e->key())
    {
    case Key_Escape:
        appCore->charEntered('\033');
        break;
    case Key_Backtab:
        appCore->charEntered(CelestiaCore::Key_BackTab);
        break;
    default:
        if (!handleSpecialKey(e, true))
        {
            if ((e->text() != 0) && (e->text() != ""))
            {
                appCore->charEntered(e->text().toUtf8().data(), modifiers);
            }
        }
    }
}


void CelestiaGlWidget::keyReleaseEvent( QKeyEvent* e )
{
    handleSpecialKey(e, false);
}


void CelestiaGlWidget::setCursorShape(CelestiaCore::CursorShape shape)
{
    Qt::CursorShape cursor;
    if (currentCursor != shape)
    {
        switch(shape)
        {
        case CelestiaCore::ArrowCursor:
            cursor = Qt::ArrowCursor;
            break;
        case CelestiaCore::UpArrowCursor:
            cursor = Qt::UpArrowCursor;
            break;
        case CelestiaCore::CrossCursor:
            cursor = Qt::CrossCursor;
            break;
        case CelestiaCore::InvertedCrossCursor:
            cursor = Qt::CrossCursor;
            break;
        case CelestiaCore::WaitCursor:
            cursor = Qt::WaitCursor;
            break;
        case CelestiaCore::BusyCursor:
            cursor = Qt::WaitCursor;
            break;
        case CelestiaCore::IbeamCursor:
            cursor = Qt::IBeamCursor;
            break;
        case CelestiaCore::SizeVerCursor:
            cursor = Qt::SizeVerCursor;
            break;
        case CelestiaCore::SizeHorCursor:
            cursor = Qt::SizeHorCursor;
            break;
        case CelestiaCore::SizeBDiagCursor:
            cursor = Qt::SizeBDiagCursor;
            break;
        case CelestiaCore::SizeFDiagCursor:
            cursor = Qt::SizeFDiagCursor;
            break;
        case CelestiaCore::SizeAllCursor:
            cursor = Qt::SizeAllCursor;
            break;
        case CelestiaCore::SplitVCursor:
            cursor = Qt::SplitVCursor;
            break;
        case CelestiaCore::SplitHCursor:
            cursor = Qt::SplitHCursor;
            break;
        case CelestiaCore::PointingHandCursor:
            cursor = Qt::PointingHandCursor;
            break;
        case CelestiaCore::ForbiddenCursor:
            cursor = Qt::ForbiddenCursor;
            break;
        case CelestiaCore::WhatsThisCursor:
            cursor = Qt::WhatsThisCursor;
            break;
        default:
            cursor = Qt::CrossCursor;
            break;
        }
        setCursor(QCursor(cursor));
        currentCursor = shape;
    }
}


CelestiaCore::CursorShape CelestiaGlWidget::getCursorShape() const
{
    return currentCursor;
}


QSize CelestiaGlWidget::sizeHint() const
{
    return QSize(640, 480);
}
