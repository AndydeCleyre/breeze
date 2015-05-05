/*
* Copyright 2014  Martin Gräßlin <mgraesslin@kde.org>
* Copyright 2014  Hugo Pereira Da Costa <hugo.pereira@free.fr>
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of
* the License or (at your option) version 3 or any later version
* accepted by the membership of KDE e.V. (or its successor approved
* by the membership of KDE e.V.), which shall act as a proxy
* defined in Section 14 of version 3 of the license.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "breezedecoration.h"

#include "breeze.h"
#include "breezesettingsprovider.h"
#include "config-breeze.h"
#include "config/breezeconfigwidget.h"

#include "breezebutton.h"
#include "breezesizegrip.h"

#include <KDecoration2/DecoratedClient>
#include <KDecoration2/DecorationButtonGroup>
#include <KDecoration2/DecorationSettings>
#include <KDecoration2/DecorationShadow>

#include <KConfigGroup>
#include <KColorUtils>
#include <KSharedConfig>
#include <KPluginFactory>

#include <QPainter>
#include <QTextStream>

#if BREEZE_HAVE_X11
#include <QX11Info>
#endif

#include <cmath>

K_PLUGIN_FACTORY_WITH_JSON(
    BreezeDecoFactory,
    "breeze.json",
    registerPlugin<Breeze::Decoration>();
    registerPlugin<Breeze::Button>(QStringLiteral("button"));
    registerPlugin<Breeze::ConfigWidget>(QStringLiteral("kcmodule"));
)

namespace Breeze
{


    //________________________________________________________________
    static int g_sDecoCount = 0;
    static int g_shadowSize = 0;
    static int g_shadowStrength = 0;
    static QSharedPointer<KDecoration2::DecorationShadow> g_sShadow;

    //________________________________________________________________
    Decoration::Decoration(QObject *parent, const QVariantList &args)
        : KDecoration2::Decoration(parent, args)
        , m_colorSettings()
        , m_animation( new QPropertyAnimation( this ) )
    {
        g_sDecoCount++;
    }

    //________________________________________________________________
    Decoration::~Decoration()
    {
        g_sDecoCount--;
        if (g_sDecoCount == 0) {
            // last deco destroyed, clean up shadow
            g_sShadow.clear();
        }

        deleteSizeGrip();

    }

    //________________________________________________________________
    void Decoration::setOpacity( qreal value )
    {
        if( m_opacity == value ) return;
        m_opacity = value;
        update();

        if( m_sizeGrip ) m_sizeGrip->update();
    }

    //________________________________________________________________
    QColor Decoration::titleBarColor() const
    {

        if( hideTitleBar() ) return m_colorSettings.titleBar( false );
        else if( m_animation->state() == QPropertyAnimation::Running )
        {
            return KColorUtils::mix( m_colorSettings.inactiveTitleBar(), m_colorSettings.activeTitleBar(), m_opacity );
        } else return m_colorSettings.titleBar( client().data()->isActive() );

    }

    //________________________________________________________________
    QColor Decoration::outlineColor() const
    {
        if( !m_useSeparator ) return QColor();
        if( m_animation->state() == QPropertyAnimation::Running )
        {
            QColor color( m_colorSettings.palette().color( QPalette::Highlight ) );
            color.setAlpha( color.alpha()*m_opacity );
            return color;
        } else if( client().data()->isActive() ) return m_colorSettings.palette().color( QPalette::Highlight );
        else return QColor();
    }

    //________________________________________________________________
    QColor Decoration::fontColor() const
    {

        if( m_animation->state() == QPropertyAnimation::Running )
        {
            return KColorUtils::mix( m_colorSettings.inactiveFont(), m_colorSettings.activeFont(), m_opacity );
        } else return m_colorSettings.font( client().data()->isActive() );

    }

    //________________________________________________________________
    void Decoration::init()
    {
        m_colorSettings.update(client().data()->palette(), *client().data());
        m_useSeparator = (m_colorSettings.palette().color( QPalette::Window ) != m_colorSettings.activeTitleBar() );


        // active state change animation
        m_animation->setStartValue( 0 );
        m_animation->setEndValue( 1.0 );
        m_animation->setTargetObject( this );
        m_animation->setPropertyName( "opacity" );
        m_animation->setEasingCurve( QEasingCurve::InOutQuad );

        reconfigure();
        updateTitleBar();
        auto s = settings();
        connect(s.data(), &KDecoration2::DecorationSettings::borderSizeChanged, this, &Decoration::recalculateBorders);

        // a change in font might cause the borders to change
        connect(s.data(), &KDecoration2::DecorationSettings::fontChanged, this, &Decoration::recalculateBorders);
        connect(s.data(), &KDecoration2::DecorationSettings::spacingChanged, this, &Decoration::recalculateBorders);

        // full reconfiguration
        connect(s.data(), &KDecoration2::DecorationSettings::reconfigured, this, &Decoration::reconfigure);
        connect(s.data(), &KDecoration2::DecorationSettings::reconfigured, SettingsProvider::self(), &SettingsProvider::reconfigure, Qt::UniqueConnection );

        connect(client().data(), &KDecoration2::DecoratedClient::adjacentScreenEdgesChanged, this, &Decoration::recalculateBorders);
        connect(client().data(), &KDecoration2::DecoratedClient::maximizedHorizontallyChanged, this, &Decoration::recalculateBorders);
        connect(client().data(), &KDecoration2::DecoratedClient::maximizedVerticallyChanged, this, &Decoration::recalculateBorders);
        connect(client().data(), &KDecoration2::DecoratedClient::captionChanged, this,
            [this]()
            {
                // update the caption area
                update(titleBar());
            }
        );

        connect(client().data(), &KDecoration2::DecoratedClient::activeChanged, this, &Decoration::updateAnimationState);
        connect(client().data(), &KDecoration2::DecoratedClient::paletteChanged, this,
            [this]() {
                m_colorSettings.update(client().data()->palette(), *client().data());
                m_useSeparator = (m_colorSettings.palette().color( QPalette::Window ) != m_colorSettings.activeTitleBar() );
                update();
            }
        );
        connect(client().data(), &KDecoration2::DecoratedClient::widthChanged, this, &Decoration::updateTitleBar);
        connect(client().data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::updateTitleBar);
        connect(client().data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::setOpaque);

        connect(client().data(), &KDecoration2::DecoratedClient::widthChanged, this, &Decoration::updateButtonsGeometry);
        connect(client().data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Decoration::updateButtonsGeometry);
        connect(client().data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Decoration::recalculateBorders);
        connect(client().data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Decoration::updateButtonsGeometry);

        createButtons();
        createShadow();
    }

    //________________________________________________________________
    void Decoration::updateTitleBar()
    {
        auto s = settings();
        const bool maximized = isMaximized();
        const int width =  maximized ? client().data()->width() : client().data()->width() - 2*s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const int height = maximized ? borderTop() : borderTop() - s->smallSpacing()*Metrics::TitleBar_TopMargin;
        const int x = maximized ? 0 : s->largeSpacing()*Metrics::TitleBar_SideMargin;
        const int y = maximized ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
        setTitleBar(QRect(x, y, width, height));
    }

    //________________________________________________________________
    void Decoration::updateAnimationState()
    {
        if( m_internalSettings->animationsEnabled() )
        {

            m_animation->setDirection( client().data()->isActive() ? QPropertyAnimation::Forward : QPropertyAnimation::Backward );
            if( m_animation->state() != QPropertyAnimation::Running ) m_animation->start();

        } else {

            update();

        }
    }

    //________________________________________________________________
    void Decoration::updateSizeGripVisibility()
    {
        auto c = client().data();
        if( m_sizeGrip )
        { m_sizeGrip->setVisible( c->isResizeable() && !isMaximized() && !c->isShaded() ); }
    }

    //________________________________________________________________
    int Decoration::borderSize(bool bottom) const
    {
        const int baseSize = settings()->smallSpacing();

        auto maxSize = [] (int size) {
            const int minSize = 4;
            return qMax(size, minSize);
        };

        if( m_internalSettings && (m_internalSettings->mask() & BorderSize ) )
        {
            switch (m_internalSettings->borderSize()) {
                case InternalSettings::BorderNone: return 0;
                case InternalSettings::BorderNoSides: return bottom ? maxSize(baseSize) : 0;
                default:
                case InternalSettings::BorderTiny: return maxSize(baseSize);
                case InternalSettings::BorderNormal: return maxSize(baseSize*2);
                case InternalSettings::BorderLarge: return maxSize(baseSize * 3);
                case InternalSettings::BorderVeryLarge: return maxSize(baseSize * 4);
                case InternalSettings::BorderHuge: return maxSize(baseSize * 5);
                case InternalSettings::BorderVeryHuge: return maxSize(baseSize * 6);
                case InternalSettings::BorderOversized: return maxSize(baseSize * 10);
            }

        } else {

            switch (settings()->borderSize()) {
                case KDecoration2::BorderSize::None: return 0;
                case KDecoration2::BorderSize::NoSides: return bottom ? maxSize(baseSize ) : 0;
                default:
                case KDecoration2::BorderSize::Tiny: return maxSize(baseSize);
                case KDecoration2::BorderSize::Normal: return maxSize(baseSize*2);
                case KDecoration2::BorderSize::Large: return maxSize(baseSize * 3);
                case KDecoration2::BorderSize::VeryLarge: return maxSize(baseSize * 4);
                case KDecoration2::BorderSize::Huge: return maxSize(baseSize * 5);
                case KDecoration2::BorderSize::VeryHuge: return maxSize(baseSize * 6);
                case KDecoration2::BorderSize::Oversized: return maxSize(baseSize * 10);

            }

        }
    }

    //________________________________________________________________
    void Decoration::reconfigure()
    {

        m_internalSettings = SettingsProvider::self()->internalSettings( this );

        // animation
        m_animation->setDuration( m_internalSettings->animationsDuration() );

        // borders
        recalculateBorders();

        // shadow
        createShadow();

        // size grip
        if( hasNoBorders() && m_internalSettings->drawSizeGrip() ) createSizeGrip();
        else deleteSizeGrip();

    }

    //________________________________________________________________
    void Decoration::recalculateBorders()
    {
        auto s = settings();
        const auto c = client().data();
        const Qt::Edges edges = c->adjacentScreenEdges();

        // left, right and bottom borders
        auto testFlag = [&]( Qt::Edge edge ) { return edges.testFlag(edge) && !m_internalSettings->drawBorderOnMaximizedWindows(); };
        const int left   = isMaximizedHorizontally() || testFlag(Qt::LeftEdge) ? 0 : borderSize();
        const int right  = isMaximizedHorizontally() || testFlag(Qt::RightEdge) ? 0 : borderSize();
        const int bottom = isMaximizedVertically() || c->isShaded() || testFlag(Qt::BottomEdge) ? 0 : borderSize(true);

        int top = 0;
        if( hideTitleBar() ) top = bottom;
        else {

            QFontMetrics fm(s->font());
            top += qMax(fm.boundingRect(c->caption()).height(), buttonHeight() );

            // padding below
            // extra pixel is used for the active window outline
            const int baseSize = settings()->smallSpacing();
            top += baseSize*Metrics::TitleBar_BottomMargin + 1;

            // padding above
            top += baseSize*TitleBar_TopMargin;

        }

        setBorders(QMargins(left, top, right, bottom));

        // extended sizes
        const int extSize = s->largeSpacing();
        int extSides = 0;
        int extBottom = 0;
        if( hasNoBorders() )
        {
            extSides = extSize;
            extBottom = extSize;

        } else if( hasNoSideBorders() ) {

            extSides = extSize;

        }

        setResizeOnlyBorders(QMargins(extSides, 0, extSides, extBottom));
    }

    //________________________________________________________________
    void Decoration::createButtons()
    {
        m_leftButtons = new KDecoration2::DecorationButtonGroup(KDecoration2::DecorationButtonGroup::Position::Left, this, &Button::create);
        m_rightButtons = new KDecoration2::DecorationButtonGroup(KDecoration2::DecorationButtonGroup::Position::Right, this, &Button::create);
        updateButtonsGeometry();
    }

    //________________________________________________________________
    void Decoration::updateButtonsGeometry()
    {
        auto s = settings();

        // adjust button position
        const int bHeight = captionHeight() + (isMaximized() ? s->smallSpacing()*Metrics::TitleBar_TopMargin:0);
        const int bWidth = buttonHeight();
        const int verticalOffset = (isMaximized() ? s->smallSpacing()*Metrics::TitleBar_TopMargin:0) + (captionHeight()-buttonHeight())/2;
        foreach( const QPointer<KDecoration2::DecorationButton>& button, m_leftButtons->buttons() + m_rightButtons->buttons() )
        {
            button.data()->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth, bHeight ) ) );
            static_cast<Button*>( button.data() )->setOffset( QPointF( 0, verticalOffset ) );
            static_cast<Button*>( button.data() )->setIconSize( QSize( bWidth, bWidth ) );
        }

        // left buttons
        if( !m_leftButtons->buttons().isEmpty() )
        {

            // spacing
            m_leftButtons->setSpacing(s->smallSpacing()*Metrics::TitleBar_ButtonSpacing);

            // padding
            const int vPadding = isMaximized() ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing()*Metrics::TitleBar_SideMargin;
            if( isMaximizedHorizontally() )
            {
                // add offsets on the side buttons, to preserve padding, but satisfy Fitts law
                auto button = static_cast<Button*>( m_leftButtons->buttons().front().data() );
                button->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth + hPadding, bHeight ) ) );
                button->setFlag( Button::FlagFirstInList );
                button->setHorizontalOffset( hPadding );

                m_leftButtons->setPos(QPointF(0, vPadding));

            } else m_leftButtons->setPos(QPointF(hPadding + borderLeft(), vPadding));

        }

        // right buttons
        if( !m_rightButtons->buttons().isEmpty() )
        {

            // spacing
            m_rightButtons->setSpacing(s->smallSpacing()*Metrics::TitleBar_ButtonSpacing);

            // padding
            const int vPadding = isMaximized() ? 0 : s->smallSpacing()*Metrics::TitleBar_TopMargin;
            const int hPadding = s->smallSpacing()*Metrics::TitleBar_SideMargin;
            if( isMaximizedHorizontally() )
            {

                auto button = static_cast<Button*>( m_rightButtons->buttons().back().data() );
                button->setGeometry( QRectF( QPoint( 0, 0 ), QSizeF( bWidth + hPadding, bHeight ) ) );
                button->setFlag( Button::FlagLastInList );

                m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width(), vPadding));

            } else m_rightButtons->setPos(QPointF(size().width() - m_rightButtons->geometry().width() - hPadding - borderRight(), vPadding));

        }


    }

    //________________________________________________________________
    void Decoration::paint(QPainter *painter, const QRect &repaintRegion)
    {
        // TODO: optimize based on repaintRegion

        // paint background
        if( !client().data()->isShaded() )
        {
            painter->fillRect(rect(), Qt::transparent);
            painter->save();
            painter->setRenderHint(QPainter::Antialiasing);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_colorSettings.frame(client().data()->isActive()));

            // clip away the top part
            if( !hideTitleBar() ) painter->setClipRect(0, borderTop(), size().width(), size().height() - borderTop(), Qt::IntersectClip);

            painter->drawRoundedRect(rect(), Metrics::Frame_FrameRadius, Metrics::Frame_FrameRadius);
            painter->restore();
        }

        if( !hideTitleBar() ) paintTitleBar(painter, repaintRegion);
    }

    //________________________________________________________________
    void Decoration::paintTitleBar(QPainter *painter, const QRect &repaintRegion)
    {
        const auto c = client().data();
        const QRect titleRect(QPoint(0, 0), QSize(size().width(), borderTop()));

        // render a linear gradient on title area
        const QColor titleBarColor( this->titleBarColor() );
        QLinearGradient gradient( 0, 0, 0, titleRect.height() );
        gradient.setColorAt(0.0, titleBarColor.lighter(100.0));
        gradient.setColorAt(0.8, titleBarColor);

        painter->save();
        painter->setBrush(gradient);
        painter->setPen(Qt::NoPen);

        if (isMaximized())
        {

            painter->drawRect(titleRect);

        } else if( c->isShaded() ) {

            painter->drawRoundedRect(titleRect, Metrics::Frame_FrameRadius, Metrics::Frame_FrameRadius);

        } else {

            // we make the rect a little bit larger to be able to clip away the rounded corners on bottom
            painter->setClipRect(titleRect, Qt::IntersectClip);
            painter->drawRoundedRect(titleRect.adjusted(0, 0, 0, Metrics::Frame_FrameRadius), Metrics::Frame_FrameRadius, Metrics::Frame_FrameRadius);

        }

        auto s = settings();

        const QColor outlineColor( this->outlineColor() );
        if( !c->isShaded() && outlineColor.isValid() )
        {
            // outline
            painter->setRenderHint( QPainter::Antialiasing, false );
            painter->setBrush( Qt::NoBrush );
            painter->setPen( outlineColor );
            painter->drawLine( titleRect.bottomLeft(), titleRect.bottomRight() );
        }

        painter->restore();

        // draw caption
        painter->setFont(s->font());
        painter->setPen(m_colorSettings.font(c->isActive()));
        const auto cR = captionRect();
        const QString caption = painter->fontMetrics().elidedText(c->caption(), Qt::ElideMiddle, cR.first.width());
        painter->drawText(cR.first, cR.second | Qt::TextSingleLine, caption);

        // draw all buttons
        m_leftButtons->paint(painter, repaintRegion);
        m_rightButtons->paint(painter, repaintRegion);
    }

    //________________________________________________________________
    int Decoration::buttonHeight() const
    {
        const int baseSize = settings()->gridUnit();
        switch( m_internalSettings->buttonSize() )
        {
            case Breeze::InternalSettings::ButtonSmall: return baseSize*1.5;
            default:
            case Breeze::InternalSettings::ButtonDefault: return baseSize*2;
            case Breeze::InternalSettings::ButtonLarge: return baseSize*2.5;
            case Breeze::InternalSettings::ButtonVeryLarge: return baseSize*3.5;
        }

    }

    //________________________________________________________________
    int Decoration::captionHeight() const
    { return hideTitleBar() ? borderTop() : borderTop() - settings()->smallSpacing()*(Metrics::TitleBar_BottomMargin + Metrics::TitleBar_TopMargin ) - 1; }

    //________________________________________________________________
    QPair<QRect,Qt::Alignment> Decoration::captionRect() const
    {
        if( hideTitleBar() ) return qMakePair( QRect(), Qt::AlignCenter );
        else {

            const int leftOffset = m_leftButtons->geometry().x() + m_leftButtons->geometry().width() + Metrics::TitleBar_SideMargin*settings()->smallSpacing();
            const int rightOffset = size().width() - m_rightButtons->geometry().x() + Metrics::TitleBar_SideMargin*settings()->smallSpacing();
            const int yOffset = settings()->smallSpacing()*Metrics::TitleBar_TopMargin;
            const QRect maxRect( leftOffset, yOffset, size().width() - leftOffset - rightOffset, captionHeight() );

            switch( m_internalSettings->titleAlignment() )
            {
                case Breeze::InternalSettings::AlignLeft:
                return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignLeft );

                case Breeze::InternalSettings::AlignRight:
                return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignRight );

                case Breeze::InternalSettings::AlignCenter:
                return qMakePair( maxRect, Qt::AlignCenter );

                default:
                case Breeze::InternalSettings::AlignCenterFullWidth:
                {

                    // full caption rect
                    const QRect fullRect = QRect( 0, yOffset, size().width(), captionHeight() );
                    QRect boundingRect( settings()->fontMetrics().boundingRect( client().data()->caption()).toRect() );

                    // text bounding rect
                    boundingRect.setTop( yOffset );
                    boundingRect.setHeight( captionHeight() );
                    boundingRect.moveLeft( ( size().width() - boundingRect.width() )/2 );

                    if( boundingRect.left() < leftOffset ) return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignLeft );
                    else if( boundingRect.right() > size().width() - rightOffset ) return qMakePair( maxRect, Qt::AlignVCenter|Qt::AlignRight );
                    else return qMakePair(fullRect, Qt::AlignCenter);

                }

            }

        }

    }

    //________________________________________________________________
    void Decoration::createShadow()
    {

        // assign global shadow if exists and parameters match
        if( g_sShadow && g_shadowSize == m_internalSettings->shadowSize() && g_shadowStrength == m_internalSettings->shadowStrength() )
        {
            setShadow(g_sShadow);
            return;
        }

        // assign parameters
        g_shadowSize = m_internalSettings->shadowSize();
        g_shadowStrength = m_internalSettings->shadowStrength();

        // setup shadow
        const int shadowOffset = qMax( 6*g_shadowSize/16, Metrics::Shadow_Overlap*2 );
        auto decorationShadow = QSharedPointer<KDecoration2::DecorationShadow>::create();
        decorationShadow->setPadding( QMargins(
            g_shadowSize-shadowOffset,
            g_shadowSize-shadowOffset,
            g_shadowSize,
            g_shadowSize ) );

        decorationShadow->setInnerShadowRect(QRect(
            g_shadowSize-shadowOffset+Metrics::Shadow_Overlap,
            g_shadowSize-shadowOffset+Metrics::Shadow_Overlap,
            shadowOffset - 2*Metrics::Shadow_Overlap,
            shadowOffset - 2*Metrics::Shadow_Overlap ) );

        // create image
        QImage image(2*g_shadowSize, 2*g_shadowSize, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);

        // create gradient
        // gaussian delta function
        auto alpha = [](qreal x) { return std::exp( -x*x/0.15 ); };

        // color calculation delta function
        auto gradientStopColor = [](QColor color, int alpha)
        {
            color.setAlpha(alpha);
            return color;
        };

        const QColor shadowColor( m_colorSettings.palette().color( QPalette::Shadow ) );

        QRadialGradient radialGradient( g_shadowSize, g_shadowSize, g_shadowSize);
        for( int i = 0; i < 10; ++i )
        {
            const qreal x( qreal( i )/9 );
            radialGradient.setColorAt(x,  gradientStopColor(shadowColor, alpha(x)*g_shadowStrength));
        }

        radialGradient.setColorAt(1, gradientStopColor( shadowColor, 0 ) );

        // fill
        QPainter painter(&image);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.fillRect( image.rect(), radialGradient);

        // contrast pixel
        painter.setBrush( Qt::NoBrush );
        painter.setPen( gradientStopColor(shadowColor, g_shadowStrength) );
        painter.setRenderHints(QPainter::Antialiasing );
        painter.drawRoundedRect( QRect( g_shadowSize-shadowOffset, g_shadowSize-shadowOffset, shadowOffset, shadowOffset ), 3, 3 );
        painter.end();

        // assign to shadow
        decorationShadow->setShadow(image);

        g_sShadow = decorationShadow;
        setShadow(decorationShadow);
    }

    //_________________________________________________________________
    void Decoration::createSizeGrip( void )
    {

        // do nothing if size grip already exist
        if( m_sizeGrip ) return;

        #if BREEZE_HAVE_X11
        if( !QX11Info::isPlatformX11() ) return;

        // access client
        KDecoration2::DecoratedClient *c( client().data() );
        if( !c ) return;

        if( c->windowId() != 0 )
        {
            m_sizeGrip = new SizeGrip( this );
            connect( client().data(), &KDecoration2::DecoratedClient::maximizedChanged, this, &Breeze::Decoration::updateSizeGripVisibility );
            connect( client().data(), &KDecoration2::DecoratedClient::shadedChanged, this, &Breeze::Decoration::updateSizeGripVisibility );
            connect( client().data(), &KDecoration2::DecoratedClient::resizeableChanged, this, &Breeze::Decoration::updateSizeGripVisibility );
        }
        #endif

    }

    //_________________________________________________________________
    void Decoration::deleteSizeGrip( void )
    {
        if( m_sizeGrip )
        {
            m_sizeGrip->deleteLater();
            m_sizeGrip = nullptr;
        }
    }

} // namespace


#include "breezedecoration.moc"
