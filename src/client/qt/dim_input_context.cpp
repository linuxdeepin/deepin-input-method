#include <QtGui>
#include <qpa/qplatforminputcontext.h>
#include <qpa/qplatforminputcontextplugin_p.h>
#include "dim_input_context.moc"

class Q_GUI_EXPORT DeepinInputContext: public QPlatformInputContext
{
    Q_OBJECT

public:
    DeepinInputContext() {}
    virtual ~DeepinInputContext() {}

    virtual bool isValid() const {return true;}
    virtual bool hasCapability(QPlatformInputContext::Capability capability) const {return true;}

    virtual void reset() {
        _predit = QString();
    }

    QString _predit;

    virtual void commit() {
        QPlatformInputContext::commit();

        QObject *input = qApp->focusObject();
        if (!input) {
            _predit = QString();
            return;
        }

        if (!_predit.isEmpty()) {
            QInputMethodEvent event;
            event.setCommitString(_predit);
            QCoreApplication::sendEvent(input, &event);
        }

        _predit = QString();
    }

    //virtual void update(Qt::InputMethodQueries);
    //virtual void invokeAction(QInputMethod::Action, int cursorPosition);
    virtual bool filterEvent(const QEvent *event) {
        if (!inputMethodAccepted())
            return false;

        const QKeyEvent *keyEvent = static_cast<const QKeyEvent *>(event);
        if (keyEvent->type() != QEvent::KeyPress) {
            commit();
            return false;
        }

        const int qtcode = keyEvent->key();
        if (qtcode >= Qt::Key_0 && qtcode <= Qt::Key_9) {
            _predit = QString("%1%1").arg(qtcode);
            return true;
        } 
        return false;
    }

    //virtual QRectF keyboardRect() const;
    //void emitKeyboardRectChanged();

    //virtual bool isAnimating() const;
    //void emitAnimatingChanged();

    //virtual void showInputPanel();
    //virtual void hideInputPanel();
    //virtual bool isInputPanelVisible() const;
    //void emitInputPanelVisibleChanged();

    //virtual QLocale locale() const;
    //void emitLocaleChanged();
    //virtual Qt::LayoutDirection inputDirection() const;
    //void emitInputDirectionChanged(Qt::LayoutDirection newDirection);

    //virtual void setFocusObject(QObject *object);
    //bool inputMethodAccepted() const;

private:
    
};

class DeepinPlatformInputContextPlugin : public QPlatformInputContextPlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID QPlatformInputContextFactoryInterface_iid FILE "dim.json")

    public:
        DeepinInputContext *create(const QString& system, const QStringList& paramList) Q_DECL_OVERRIDE
        {
            qDebug() << __func__ << system;
            Q_UNUSED(paramList);
            if (system.compare(system, QLatin1String("dim"), Qt::CaseInsensitive) == 0) {
                return new DeepinInputContext;
            }
            return 0;
        }
};
