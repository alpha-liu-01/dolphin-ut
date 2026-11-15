// Stage 1 step 5: does a Qt 6 Widgets window work on Lomiri's Wayland, the way DolphinQt uses it?
//
// Shows a Widgets main window with:
//   - labels and a button (raster widgets), a line edit (on-screen keyboard),
//   - a QOpenGLWindow in a window container (Qt's own GLES through wayland-egl),
//   - a native child QWidget (WA_NativeWindow), whose wl_surface is read the way
//     DolphinQt's GetWindowSystemInfo() reads it,
// then, after a few seconds, a second top-level widget shown fullscreen (like Dolphin's render
// window) for a few seconds. Logs every step, every touch/mouse event and the GL frame rate.
//
// Usage: qt6-probe [--seconds N] [--no-second-window] [--no-gl] [--no-native-child]

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QOpenGLFunctions>
#include <QOpenGLWindow>
#include <QPushButton>
#include <QScreen>
#include <QTimer>
#include <QTouchEvent>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>
#include <qpa/qplatformnativeinterface.h>

#include <cstdio>

static QElapsedTimer s_clock;

#define LOG(...)                                                                                   \
  do                                                                                               \
  {                                                                                                \
    std::printf("[%7.3f] ", s_clock.elapsed() / 1000.0);                                           \
    std::printf(__VA_ARGS__);                                                                      \
    std::printf("\n");                                                                             \
    std::fflush(stdout);                                                                           \
  } while (0)

static void LogNative(const char* what, QWindow* window)
{
  QPlatformNativeInterface* pni = QGuiApplication::platformNativeInterface();
  void* display = pni ? pni->nativeResourceForWindow("display", window) : nullptr;
  void* surface = pni ? pni->nativeResourceForWindow("surface", window) : nullptr;
  LOG("%s: winId=0x%llx wl_display=%p wl_surface=%p size=%dx%d dpr=%.2f", what,
      window ? static_cast<unsigned long long>(window->winId()) : 0ULL, display, surface,
      window ? window->width() : 0, window ? window->height() : 0,
      window ? window->devicePixelRatio() : 0.0);
}

class InputLogger : public QObject
{
public:
  using QObject::QObject;

protected:
  bool eventFilter(QObject* obj, QEvent* e) override
  {
    switch (e->type())
    {
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    {
      auto* t = static_cast<QTouchEvent*>(e);
      if (e->type() != QEvent::TouchUpdate && !t->points().isEmpty())
        LOG("touch %s on %s: %lld point(s), first at %.0f,%.0f",
            e->type() == QEvent::TouchBegin ? "begin" : "end",
            obj->objectName().toUtf8().constData(), static_cast<long long>(t->points().size()),
            t->points().first().position().x(), t->points().first().position().y());
      break;
    }
    case QEvent::MouseButtonPress:
    {
      auto* m = static_cast<QMouseEvent*>(e);
      LOG("mouse press on %s at %.0f,%.0f (source %s)", obj->objectName().toUtf8().constData(),
          m->position().x(), m->position().y(),
          m->device() ? m->device()->name().toUtf8().constData() : "?");
      break;
    }
    case QEvent::WindowStateChange:
      if (auto* w = qobject_cast<QWidget*>(obj))
        LOG("window state of %s: 0x%x", obj->objectName().toUtf8().constData(),
            static_cast<int>(w->windowState()));
      break;
    default:
      break;
    }
    return false;
  }
};

class GLWindow : public QOpenGLWindow, protected QOpenGLFunctions
{
protected:
  void initializeGL() override
  {
    initializeOpenGLFunctions();
    LOG("QOpenGLWindow: GL_RENDERER=%s GL_VERSION=%s",
        reinterpret_cast<const char*>(glGetString(GL_RENDERER)),
        reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    m_timer.start();
  }
  void paintGL() override
  {
    const float t = (m_frames % 120) / 120.0f;
    glClearColor(0.1f, 0.3f + 0.6f * t, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (++m_frames % 240 == 0)
    {
      LOG("QOpenGLWindow: %d frames, %.1f fps over the last 240, size %dx%d", m_frames,
          240000.0 / m_timer.restart(), width(), height());
    }
    update();
  }

private:
  int m_frames = 0;
  QElapsedTimer m_timer;
};

int main(int argc, char** argv)
{
  s_clock.start();
  QApplication app(argc, argv);
  int seconds = 60;
  bool second_window = true;
  bool with_gl = true;
  bool with_native_child = true;
  const QStringList args = app.arguments();
  for (int i = 1; i < args.size(); ++i)
  {
    if (args[i] == QStringLiteral("--seconds") && i + 1 < args.size())
      seconds = args[++i].toInt();
    else if (args[i] == QStringLiteral("--no-second-window"))
      second_window = false;
    else if (args[i] == QStringLiteral("--no-gl"))
      with_gl = false;
    else if (args[i] == QStringLiteral("--no-native-child"))
      with_native_child = false;
  }

  LOG("Qt %s, platform '%s'", qVersion(), QGuiApplication::platformName().toUtf8().constData());
  if (QScreen* s = QGuiApplication::primaryScreen())
    LOG("screen '%s' %dx%d available %dx%d dpr=%.2f logical dpi=%.1f refresh=%.2f",
        s->name().toUtf8().constData(), s->geometry().width(), s->geometry().height(),
        s->availableGeometry().width(), s->availableGeometry().height(), s->devicePixelRatio(),
        s->logicalDotsPerInch(), s->refreshRate());

  InputLogger logger;
  app.installEventFilter(&logger);

  QWidget main_window;
  main_window.setObjectName(QStringLiteral("main"));
  main_window.setWindowTitle(QStringLiteral("dolphin-ut Qt 6 probe"));
  auto* layout = new QVBoxLayout(&main_window);
  auto* label = new QLabel(QStringLiteral("Qt %1 on '%2'\nTap the button, then the text field.")
                               .arg(QString::fromLatin1(qVersion()), QGuiApplication::platformName()));
  label->setObjectName(QStringLiteral("label"));
  label->setWordWrap(true);
  auto* button = new QPushButton(QStringLiteral("Tap me"));
  button->setObjectName(QStringLiteral("button"));
  button->setMinimumHeight(120);
  int taps = 0;
  QObject::connect(button, &QPushButton::clicked, [&] {
    ++taps;
    button->setText(QStringLiteral("Tapped %1").arg(taps));
    LOG("button clicked (%d)", taps);
  });
  auto* edit = new QLineEdit;
  edit->setObjectName(QStringLiteral("edit"));
  edit->setPlaceholderText(QStringLiteral("Type here (on-screen keyboard)"));
  QObject::connect(edit, &QLineEdit::textChanged,
                   [](const QString& t) { LOG("text: '%s'", t.toUtf8().constData()); });

  auto* gl_window = new GLWindow;
  QWidget* gl_container = QWidget::createWindowContainer(gl_window);
  gl_container->setObjectName(QStringLiteral("glcontainer"));
  gl_container->setMinimumHeight(400);

  // A native child widget, like DolphinQt's RenderWidget with "Render to Main Window".
  auto* native_child = new QWidget;
  native_child->setObjectName(QStringLiteral("nativechild"));
  native_child->setAttribute(Qt::WA_NativeWindow);
  native_child->setAutoFillBackground(true);
  QPalette pal = native_child->palette();
  pal.setColor(QPalette::Window, QColor(200, 80, 40));
  native_child->setPalette(pal);
  native_child->setMinimumHeight(200);

  auto* row = new QHBoxLayout;
  row->addWidget(button);
  layout->addWidget(label);
  layout->addLayout(row);
  layout->addWidget(edit);
  if (with_gl)
    layout->addWidget(gl_container, 1);
  else
    layout->addStretch(1);
  if (with_native_child)
    layout->addWidget(native_child);
  main_window.show();
  main_window.installEventFilter(&logger);

  QTimer::singleShot(1500, [&] {
    LogNative("main window", main_window.windowHandle());
    if (with_native_child)
      LogNative("native child", native_child->windowHandle());
    if (with_gl)
      LogNative("GL window", gl_window);
  });

  QWidget* render_window = nullptr;
  if (second_window)
  {
    QTimer::singleShot(4000, [&] {
      render_window = new QWidget;
      render_window->setObjectName(QStringLiteral("render"));
      render_window->setWindowTitle(QStringLiteral("dolphin-ut probe render window"));
      render_window->setAttribute(Qt::WA_NativeWindow);
      render_window->setAutoFillBackground(true);
      QPalette rp = render_window->palette();
      rp.setColor(QPalette::Window, QColor(40, 40, 160));
      render_window->setPalette(rp);
      render_window->installEventFilter(&logger);
      render_window->showFullScreen();
      LOG("second top-level window shown fullscreen");
      QTimer::singleShot(1500, [&] { LogNative("render window", render_window->windowHandle()); });
      QTimer::singleShot(6000, [&] {
        LOG("closing the second window");
        render_window->close();
      });
    });
  }

  QTimer::singleShot(seconds * 1000, [&] {
    LOG("time is up (%d s), %d button taps", seconds, taps);
    app.quit();
  });
  const int rc = app.exec();
  LOG("exit %d", rc);
  return rc;
}
