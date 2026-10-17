#include "tts_phonomenal.h"
#include <KGlobalAccel>
#include <KWindowSystem>
#include <LayerShellQt/Window>
#include <QAction>
#include <QApplication>
#include <QAudioDevice>
#include <QAudioOutput>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QCursor>
#include <QDialog>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QInputMethodEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMediaDevices>
#include <QMediaPlayer>
#include <QMenu>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QSystemTrayIcon>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QtConcurrent>
#include <QtEndian>
#include <atomic>
#include <memory>

struct Speech {
  QByteArray audio;
  QString error;
};
class Window : public QWidget {
  QSettings settings;
  QMediaDevices devices;
  QComboBox *voice, *first, *second, *language, *microphone;
  QSpinBox *rate;
  QPlainTextEdit *text;
  QLineEdit *pack, *custom;
  QLabel *status;
  QPushButton *speakButton;
  QCheckBox *bridge;
  QCheckBox *translate, *micMuted, *fallback, *sendChat;
  QSlider *micGain;
  QSystemTrayIcon *tray;
  QTimer inputTimeout;
  QDialog playbackOverlay;
  QLabel *overlayStatus;
  QAction *toggleAction, *configAction, *stopAction;
  QStringList history;
  QString currentMessage;
  QMenu *historyMenu;
  LayerShellQt::Window *inputLayer = nullptr;
  LayerShellQt::Window *statusLayer = nullptr;
  QMediaPlayer players[3];
  QAudioOutput outputs[3];
  QProcess microsoft;
  QByteArray microsoftBuffer;
  bool microsoftPending = false;
  QString microsoftRoot() const {
    auto root = qEnvironmentVariable("VCTTS_SAPI_ROOT");
    return root.isEmpty() ? QDir::homePath() + "/.local/share/vctts/sapi"
                          : root;
  }
  QNetworkAccessManager network;
  QNetworkReply *reply = nullptr;
  QTemporaryDir temporary;
  QFutureWatcher<Speech> synthesis;
  QDialog quick;
  QLineEdit *quickText;
  QString bridgeModule;
  quint64 generation = 0;
  bool cancelled = false;
  QString pendingMessage;
  std::shared_ptr<std::atomic_bool> workerCancel;

public:
  void cancellationTest() {
    custom->setText("/usr/bin/sleep 10");
    voice->setCurrentIndex(voice->findData("custom"));
    translate->setChecked(false);
    speak("Cancellation test");
    QTimer::singleShot(100, this, [this] { stopSpeech(); });
    QTimer::singleShot(800, this, [this] {
      if (synthesis.isRunning()) {
        qCritical("FAIL: worker process was not cancelled");
        QCoreApplication::exit(5);
      } else {
        qInfo("PASS: Stop cancels a stalled custom synthesis process promptly");
        QCoreApplication::exit(0);
      }
    });
  }
  void overlayTest() {
    if (!qEnvironmentVariable("VCTTS_TEST_MIC").isEmpty()) {
      auto index = microphone->findData(qEnvironmentVariable("VCTTS_TEST_MIC"));
      if (index < 0) {
        qCritical("FAIL: silent test microphone missing");
        QCoreApplication::exit(5);
        return;
      }
      microphone->setCurrentIndex(index);
      micGain->setValue(50);
      micMuted->setChecked(false);
      bridge->setChecked(true);
      if (bridgeModule.isEmpty()) {
        qCritical("FAIL: bridge did not start");
        QCoreApplication::exit(5);
        return;
      }
      QProcess list;
      list.start("pactl", {"-f", "json", "list", "sink-inputs"});
      list.waitForFinished(3000);
      bool gainFound = false;
      for (const auto &entry :
           QJsonDocument::fromJson(list.readAllStandardOutput()).array()) {
        auto object = entry.toObject();
        if (object["owner_module"].toVariant().toString() == bridgeModule) {
          auto volumes = object["volume"].toObject();
          for (const auto &value : volumes)
            if (value.toObject()["value"].toInteger() == 32768)
              gainFound = true;
        }
      }
      if (!gainFound) {
        qCritical("FAIL: bridge gain was not applied");
        QCoreApplication::exit(5);
        return;
      }
      micMuted->setChecked(true);
      if (!bridgeModule.isEmpty()) {
        qCritical("FAIL: mic mute left bridge running");
        QCoreApplication::exit(5);
        return;
      }
      bridge->setChecked(false);
      qInfo("PASS: microphone selection, 50 percent bridge gain and mute "
            "teardown");
    }
    showSettings();
    QTimer::singleShot(200, this, [this] {
      auto advancedButton = findChild<QPushButton *>("advanced-settings");
      if (pack->isVisible() || !voice->isVisible() || !advancedButton) {
        qCritical("FAIL: simplified configuration visibility");
        QCoreApplication::exit(5);
        return;
      }
      grab().save(qEnvironmentVariable("VCTTS_TEST_SCREENSHOT") +
                  ".config.png");
      advancedButton->click();
      if (!pack->isVisible()) {
        qCritical("FAIL: advanced settings did not expand");
        QCoreApplication::exit(5);
        return;
      }
      advancedButton->click();
      hide();
      QTimer::singleShot(150, this, [this] { toggleInput(); });
    });
    QTimer::singleShot(1000, this, [this] {
      auto fail = [](const char *message) {
        qCritical("FAIL: %s", message);
        QCoreApplication::exit(5);
      };
      if (!quick.isVisible() || !quickText->hasFocus()) {
        fail("overlay did not receive keyboard focus");
        return;
      }
      if (KWindowSystem::isPlatformWayland() &&
          (!inputLayer ||
           inputLayer->layer() != LayerShellQt::Window::LayerOverlay)) {
        fail("Wayland overlay layer missing");
        return;
      }
      QInputMethodEvent input;
      input.setCommitString(
          QString::fromUtf8("Zażółć gęślą jaźń — VCTTS overlay"));
      QApplication::sendEvent(quickText, &input);
      if (quickText->text() !=
          QString::fromUtf8("Zażółć gęślą jaźń — VCTTS overlay")) {
        qWarning() << "Actual input:" << quickText->text();
        fail("Unicode input failed");
        return;
      }
      quick.grab().save(qEnvironmentVariable("VCTTS_TEST_SCREENSHOT"));
      QTest::keyClick(quickText, Qt::Key_Backspace);
      if (!quickText->text().endsWith("overla")) {
        fail("Backspace failed");
        return;
      }
      QTest::keyClick(quickText, Qt::Key_Escape);
      if (quick.isVisible() || !quickText->text().isEmpty()) {
        fail("Escape did not dismiss and clear overlay");
        return;
      }
      toggleInput();
      toggleInput();
      if (quick.isVisible()) {
        fail("hotkey toggle failed");
        return;
      }
      toggleInput();
      inputTimeout.start(20);
      QTimer::singleShot(100, this, [this, fail] {
        if (quick.isVisible()) {
          fail("idle timeout failed");
          return;
        }
        toggleInput();
        quickText->setText("VCTTS overlay input test.");
        voice->setCurrentIndex(0);
        translate->setChecked(false);
        second->setCurrentIndex(0);
        for (int i = 0; i < first->count(); ++i)
          if (first->itemText(i).contains("VCTTS")) {
            first->setCurrentIndex(i);
            break;
          }
        connect(&players[0], &QMediaPlayer::mediaStatusChanged, this,
                [](QMediaPlayer::MediaStatus state) {
                  if (state == QMediaPlayer::EndOfMedia) {
                    qInfo("PASS: native Wayland layer overlay focus, Unicode, "
                          "Backspace, Escape, toggle, timeout, Enter "
                          "submission and virtual audio playback");
                    QCoreApplication::exit(0);
                  }
                });
        QTest::keyClick(quickText, Qt::Key_Return);
        if (quick.isVisible() ||
            currentMessage != "VCTTS overlay input test.") {
          fail("Enter did not submit and dismiss");
          return;
        }
      });
    });
    QTimer::singleShot(15000, qApp, [] {
      qCritical("FAIL: overlay test timed out");
      QCoreApplication::exit(6);
    });
  }
  void selfTest(bool configured = false) {
    if (!configured) {
      refreshDevices();
      int target = -1;
      for (int i = 0; i < first->count(); ++i)
        if (first->itemText(i).contains("VCTTS")) {
          target = i;
          break;
        }
      if (target < 0) {
        qWarning("Virtual output missing");
        QCoreApplication::exit(2);
        return;
      }
      first->setCurrentIndex(target);
      second->setCurrentIndex(0);
      voice->setCurrentIndex(0);
    }
    connect(&players[0], &QMediaPlayer::mediaStatusChanged, this,
            [](QMediaPlayer::MediaStatus state) {
              if (state == QMediaPlayer::EndOfMedia) {
                qInfo("PASS: native speech synthesized and played to VCTTS "
                      "virtual audio output");
                QCoreApplication::exit(0);
              }
            });
    QTimer::singleShot(15000, qApp, [] {
      qWarning("Audio integration test timed out");
      QCoreApplication::exit(3);
    });
    speak("VCTTS Linux audio routing test.");
  }
  Window()
      : quick(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
        playbackOverlay(nullptr, Qt::Tool | Qt::FramelessWindowHint |
                                     Qt::WindowStaysOnTopHint |
                                     Qt::WindowTransparentForInput) {
    setWindowTitle("VCTTS — Linux");
    resize(620, 650);
    auto outer = new QVBoxLayout(this);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    outer->addWidget(scroll);
    auto content = new QWidget;
    scroll->setWidget(content);
    auto layout = new QFormLayout(content);
    layout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto intro = new QLabel(
        "Type to speak into voice chat and your headphones.\nCtrl+Backspace: "
        "quick input · Ctrl+Shift+Backspace: settings");
    intro->setWordWrap(true);
    layout->addRow(intro);
    voice = new QComboBox;
    if (QFile::exists(microsoftRoot() + "/speech11-bridge.exe")) {
      voice->addItem("Microsoft Zira Pro — English (offline)", "sapi-zira");
      voice->addItem("Microsoft Paulina — Polish (offline)", "sapi-paulina");
    }
    voice->addItem("English — offline", "en-us");
    voice->addItem("Polish — offline", "pl");
    voice->addItem("Phonomenal voice pack — offline", "phonomenal");
    voice->addItem("Brian — StreamElements online", "online");
    voice->addItem("Custom program (WAV to stdout)", "custom");
    voice->setCurrentIndex(
        qMax(0, voice->findData(settings.value("voice", "sapi-zira"))));
    layout->addRow("Voice", voice);
    rate = new QSpinBox;
    rate->setRange(80, 400);
    rate->setValue(settings.value("rate", 150).toInt());
    layout->addRow("Offline speech rate", rate);
    pack = new QLineEdit(settings.value("pack").toString());
    layout->addRow("Phonomenal pack", pack);
    auto browse = new QPushButton("Choose .vcpack / .phbank…");
    layout->addRow(browse);
    connect(browse, &QPushButton::clicked, this, [&] {
      auto path = QFileDialog::getOpenFileName(
          this, "Voice pack", {}, "Phonomenal (*.vcpack *.phbank)");
      if (!path.isEmpty()) {
        pack->setText(path);
        settings.setValue("pack", path);
        voice->setCurrentIndex(voice->findData("phonomenal"));
      }
    });
    custom = new QLineEdit(settings.value("custom").toString());
    custom->setPlaceholderText("Program and arguments; text is sent on stdin");
    layout->addRow("Custom TTS", custom);
    translate = new QCheckBox("Translate submitted text online (Google)");
    translate->setChecked(settings.value("translate", false).toBool());
    layout->addRow(translate);
    connect(translate, &QCheckBox::toggled, this,
            [&](bool on) { settings.setValue("translate", on); });
    fallback = new QCheckBox("Use online Brian if offline speech fails");
    fallback->setChecked(settings.value("fallback", false).toBool());
    layout->addRow(fallback);
    connect(fallback, &QCheckBox::toggled, this,
            [&](bool on) { settings.setValue("fallback", on); });
    language = new QComboBox;
    for (const auto &code : {"en", "pl", "de", "es", "fr", "uk", "ja"})
      language->addItem(code, code);
    language->setCurrentIndex(
        qMax(0, language->findData(settings.value("language", "en"))));
    layout->addRow("Translation language", language);
    connect(language, &QComboBox::currentIndexChanged, this,
            [&] { settings.setValue("language", language->currentData()); });
    first = new QComboBox;
    second = new QComboBox;
    layout->addRow("Listen on", first);
    layout->addRow("Extra output", second);
    sendChat = new QCheckBox("Send speech to Discord / virtual microphone");
    sendChat->setChecked(settings.value("sendChat", true).toBool());
    layout->addRow(sendChat);
    connect(sendChat, &QCheckBox::toggled, this,
            [this](bool enabled) { settings.setValue("sendChat", enabled); });
    auto setup = new QPushButton("Create / refresh virtual microphone");
    layout->addRow(setup);
    connect(setup, &QPushButton::clicked, this, [&] { setupAudio(); });
    bridge = new QCheckBox("Include my microphone");
    layout->addRow(bridge);
    microphone = new QComboBox;
    layout->addRow("Physical microphone", microphone);
    microphone->addItem("System default", QString("default"));
    QProcess sources;
    sources.start("pactl", {"-f", "json", "list", "sources"});
    sources.waitForFinished(3000);
    for (const auto &entry :
         QJsonDocument::fromJson(sources.readAllStandardOutput()).array()) {
      auto object = entry.toObject();
      auto name = object["name"].toString();
      if (!name.startsWith("vctts") && !name.endsWith(".monitor"))
        microphone->addItem(object["description"].toString(), name);
    }
    microphone->setCurrentIndex(
        qMax(0, microphone->findData(settings.value("microphone", "default"))));
    micMuted = new QCheckBox("Mute microphone");
    micMuted->setChecked(settings.value("micMuted", false).toBool());
    layout->addRow(micMuted);
    micGain = new QSlider(Qt::Horizontal);
    micGain->setRange(0, 150);
    micGain->setValue(settings.value("micGain", 100).toInt());
    layout->addRow("Microphone gain", micGain);
    connect(bridge, &QCheckBox::toggled, this, [&](bool on) {
      settings.setValue("bridge", on);
      setBridge(on);
    });
    connect(microphone, &QComboBox::activated, this, [&] {
      settings.setValue("microphone", microphone->currentData());
      setBridge(bridge->isChecked());
    });
    connect(micMuted, &QCheckBox::toggled, this, [&](bool on) {
      settings.setValue("micMuted", on);
      setBridge(bridge->isChecked());
    });
    connect(micGain, &QSlider::valueChanged, this,
            [&](int value) { settings.setValue("micGain", value); });
    connect(micGain, &QSlider::sliderReleased, this,
            [&] { setBridge(bridge->isChecked()); });
    for (int i = 0; i < 2; ++i) {
      auto volume = new QSlider(Qt::Horizontal);
      volume->setRange(0, 100);
      auto key = QString("volume%1").arg(i);
      volume->setValue(settings.value(key, 100).toInt());
      outputs[i].setVolume(volume->value() / 100.0);
      layout->addRow(i == 0 ? "Listen volume" : "Extra-output volume", volume);
      connect(volume, &QSlider::valueChanged, this, [this, i, key](int v) {
        outputs[i].setVolume(v / 100.0);
        settings.setValue(key, v);
      });
    }
    auto note = new QLabel(
        "In your voice-chat app, choose “VCTTS Virtual Microphone” as its "
        "input.\nYour normal microphone stays unchanged. Online Brian sends "
        "only the text you submit to StreamElements.");
    note->setWordWrap(true);
    layout->addRow(note);
    text = new QPlainTextEdit;
    text->setPlaceholderText("Type a message…");
    layout->addRow(text);
    speakButton = new QPushButton("Speak (Ctrl+Enter)");
    layout->addRow(speakButton);
    connect(speakButton, &QPushButton::clicked, this,
            [&] { speak(text->toPlainText()); });
    auto send = new QShortcut(QKeySequence("Ctrl+Return"), this);
    connect(send, &QShortcut::activated, speakButton, &QPushButton::click);
    auto stop = new QPushButton("Stop speaking");
    layout->addRow(stop);
    connect(stop, &QPushButton::clicked, this, [&] { stopSpeech(); });
    status = new QLabel("Ready");
    status->setWordWrap(true);
    layout->addRow(status);
    for (int i = 0; i < 3; ++i) {
      players[i].setAudioOutput(&outputs[i]);
      connect(&players[i], &QMediaPlayer::errorOccurred, this,
              [&](QMediaPlayer::Error, const QString &message) {
                status->setText(message);
                showProgress(message);
                QTimer::singleShot(5000, &playbackOverlay, &QWidget::hide);
              });
    }
    connect(&devices, &QMediaDevices::audioOutputsChanged, this,
            [&] { refreshDevices(); });
    refreshDevices();
    connect(voice, &QComboBox::currentIndexChanged, this,
            [&] { settings.setValue("voice", voice->currentData()); });
    connect(rate, &QSpinBox::valueChanged, this,
            [&](int value) { settings.setValue("rate", value); });
    connect(custom, &QLineEdit::editingFinished, this,
            [&] { settings.setValue("custom", custom->text()); });
    connect(pack, &QLineEdit::editingFinished, this,
            [&] { settings.setValue("pack", pack->text()); });
    for (auto box : {first, second})
      connect(box, &QComboBox::activated, this, [&, box] {
        settings.setValue(box == first ? "first" : "second",
                          box->currentData());
      });
    connect(&synthesis, &QFutureWatcher<Speech>::finished, this, [&] {
      auto result = synthesis.result();
      speakButton->setEnabled(true);
      if (cancelled) {
        if (!pendingMessage.isEmpty()) {
          auto next = pendingMessage;
          pendingMessage.clear();
          speak(next);
        }
        return;
      }
      if (!result.error.isEmpty()) {
        if (fallback->isChecked() &&
            (voice->currentData() == "en-us" || voice->currentData() == "pl")) {
          speakOnline(currentMessage);
          return;
        }
        status->setText(result.error);
        showProgress(result.error);
        QTimer::singleShot(5000, &playbackOverlay, &QWidget::hide);
      } else
        play(result.audio);
    });
    quick.setWindowTitle("VCTTS Overlay");
    quick.setAttribute(Qt::WA_TranslucentBackground);
    auto qlayout = new QVBoxLayout(&quick);
    qlayout->setContentsMargins(0, 0, 0, 0);
    auto panel = new QWidget;
    panel->setObjectName("overlayPanel");
    qlayout->addWidget(panel);
    auto panelLayout = new QVBoxLayout(panel);
    panelLayout->setContentsMargins(22, 16, 22, 18);
    auto heading =
        new QLabel("<b style='color:#91a6ff'>VOICE TTS</b> &nbsp; <span "
                   "style='color:#a4fdcc'>Recording</span> &nbsp; <span "
                   "style='color:#aebbd0'>Enter speak · Esc cancel</span>");
    panelLayout->addWidget(heading);
    quickText = new QLineEdit;
    quickText->setObjectName("overlayInput");
    quickText->setPlaceholderText("Start typing…");
    quickText->setMaxLength(4000);
    panelLayout->addWidget(quickText);
    quick.setStyleSheet(
        "#overlayPanel {background:rgba(17,19,24,235);border:1px solid "
        "#56637c;border-radius:18px;} #overlayInput "
        "{background:transparent;border:0;color:#f7faff;font-size:22px;padding:"
        "5px 0;} QLabel {color:#d0d9e9;font-size:13px;}");
    quick.resize(720, 128);
    playbackOverlay.setAttribute(Qt::WA_TranslucentBackground);
    playbackOverlay.setAttribute(Qt::WA_TransparentForMouseEvents);
    auto playbackLayout = new QVBoxLayout(&playbackOverlay);
    playbackLayout->setContentsMargins(0, 0, 0, 0);
    overlayStatus = new QLabel;
    overlayStatus->setWordWrap(true);
    overlayStatus->setMargin(18);
    overlayStatus->setStyleSheet(
        "background:rgba(17,19,24,230);color:#f7faff;border:1px solid "
        "#56637c;border-radius:16px;font-size:16px;");
    playbackLayout->addWidget(overlayStatus);
    playbackOverlay.resize(720, 100);
    configureLayer(quick, inputLayer, true);
    configureLayer(playbackOverlay, statusLayer, false);
    inputTimeout.setSingleShot(true);
    connect(&inputTimeout, &QTimer::timeout, this, [&] { commitInput(); });
    auto timeout = new QSpinBox;
    timeout->setRange(0, 300);
    timeout->setSuffix(" s (0 = off)");
    timeout->setValue(settings.value("timeout", 30).toInt());
    layout->addRow("Input idle timeout", timeout);
    connect(timeout, &QSpinBox::valueChanged, this,
            [&](int value) { settings.setValue("timeout", value); });
    connect(quickText, &QLineEdit::textChanged, this,
            [&] { restartInputTimeout(); });
    connect(quickText, &QLineEdit::returnPressed, this, [&] { commitInput(); });
    auto esc = new QShortcut(QKeySequence(Qt::Key_Escape), &quick);
    connect(esc, &QShortcut::activated, this, [&] { cancelInput(); });
    toggleAction =
        shortcut(layout, "quick-input", "Quick input", "Ctrl+Backspace");
    connect(toggleAction, &QAction::triggered, this, [&] { toggleInput(); });
    configAction =
        shortcut(layout, "settings", "Settings", "Ctrl+Shift+Backspace");
    connect(configAction, &QAction::triggered, this, [&] { showSettings(); });
    stopAction =
        shortcut(layout, "stop-speech", "Stop speech", "Ctrl+Alt+Backspace");
    connect(stopAction, &QAction::triggered, this, [&] { stopSpeech(); });
    auto done = new QPushButton("Done — run in background");
    layout->addRow(done);
    connect(done, &QPushButton::clicked, this, &QWidget::hide);
    tray = new QSystemTrayIcon(style()->standardIcon(QStyle::SP_MediaVolume),
                               this);
    tray->setToolTip("VCTTS — ready");
    auto menu = new QMenu(this);
    menu->addAction("Type a message", this, [&] { toggleInput(); });
    menu->addAction("Settings", this, [&] { showSettings(); });
    menu->addAction("Stop speaking", this, [&] { stopSpeech(); });
    historyMenu = menu->addMenu("Recent messages");
    refreshHistory();
    menu->addSeparator();
    menu->addAction("Quit VCTTS", qApp, &QApplication::quit);
    tray->setContextMenu(menu);
    tray->show();
    connect(tray, &QSystemTrayIcon::activated, this,
            [&](QSystemTrayIcon::ActivationReason reason) {
              if (reason == QSystemTrayIcon::Trigger)
                toggleInput();
            });

    for (int i = 0; i < 3; ++i)
      connect(&players[i], &QMediaPlayer::playbackStateChanged, this, [&] {
        if (players[0].playbackState() == QMediaPlayer::StoppedState &&
            players[1].playbackState() == QMediaPlayer::StoppedState &&
            players[2].playbackState() == QMediaPlayer::StoppedState)
          QTimer::singleShot(200, this, [&] {
            if (!synthesis.isRunning() && !reply && !microsoftPending &&
                players[0].playbackState() == QMediaPlayer::StoppedState &&
                players[1].playbackState() == QMediaPlayer::StoppedState &&
                players[2].playbackState() == QMediaPlayer::StoppedState)
              playbackOverlay.hide();
          });
      });
    auto advanced = new QWidget;
    auto advancedLayout = new QFormLayout(advanced);
    advancedLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    auto simpleLayout = new QFormLayout;
    simpleLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
    const QSet<QWidget *> basic = {intro,  voice,    first, sendChat,
                                   bridge, micMuted, text,  speakButton,
                                   stop,   status,   done};
    while (layout->rowCount()) {
      auto row = layout->takeRow(0);
      QWidget *field = row.fieldItem ? row.fieldItem->widget() : nullptr;
      auto destination = basic.contains(field) ? simpleLayout : advancedLayout;
      if (row.labelItem)
        destination->addRow(row.labelItem->widget(), field);
      else if (field)
        destination->addRow(field);
      delete row.labelItem;
      delete row.fieldItem;
    }
    delete layout;
    auto configLayout = new QVBoxLayout(content);
    configLayout->addLayout(simpleLayout);
    auto advancedButton = new QPushButton("Advanced settings ▸");
    advancedButton->setObjectName("advanced-settings");
    advancedButton->setCheckable(true);
    configLayout->addWidget(advancedButton);
    configLayout->addWidget(advanced);
    advanced->hide();
    connect(advancedButton, &QPushButton::toggled, this,
            [advanced, advancedButton](bool shown) {
              advanced->setVisible(shown);
              advancedButton->setText(shown ? "Advanced settings ▾"
                                            : "Advanced settings ▸");
            });
    configLayout->addStretch();
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    for (auto box : {voice, first, second, microphone}) {
      box->setSizeAdjustPolicy(
          QComboBox::AdjustToMinimumContentsLengthWithIcon);
      box->setMinimumContentsLength(18);
      box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }
    text->setMaximumHeight(80);
    resize(620, 560);
    connect(&microsoft, &QProcess::readyReadStandardOutput, this, [this] {
      microsoftBuffer += microsoft.readAllStandardOutput();
      if (microsoftBuffer.size() < 8)
        return;
      auto code = qFromLittleEndian<quint32>(
          reinterpret_cast<const uchar *>(microsoftBuffer.constData()));
      auto length = qFromLittleEndian<quint32>(
          reinterpret_cast<const uchar *>(microsoftBuffer.constData() + 4));
      if (length > 64000000) {
        microsoft.kill();
        return;
      }
      if (microsoftBuffer.size() < 8 + length)
        return;
      auto data = microsoftBuffer.mid(8, length);
      microsoftBuffer.remove(0, 8 + length);
      if (!microsoftPending)
        return;
      microsoftPending = false;
      speakButton->setEnabled(true);
      if (code) {
        status->setText(QString::fromUtf8(data));
        showProgress(status->text());
        QTimer::singleShot(5000, &playbackOverlay, &QWidget::hide);
      } else
        play(data);
    });
    connect(&microsoft, &QProcess::readyReadStandardError, this,
            [this] { microsoft.readAllStandardError(); });
    connect(&microsoft, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError) {
              if (microsoftPending) {
                microsoftPending = false;
                speakButton->setEnabled(true);
                status->setText("Microsoft speech bridge failed to start");
                showProgress(status->text());
              }
            });
    connect(&microsoft,
            qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this](int, QProcess::ExitStatus) {
              if (microsoftPending) {
                microsoftPending = false;
                speakButton->setEnabled(true);
                status->setText(
                    "Microsoft speech bridge exited before returning audio");
                showProgress(status->text());
              }
            });
    setupAudio();
    if (QFile::exists(microsoftRoot() + "/speech11-bridge.exe"))
      startMicrosoft();
    bridge->setChecked(settings.value("bridge", false).toBool());
  }
  ~Window() override {
    stopSpeech();
    setBridge(false);
    synthesis.waitForFinished();
    microsoft.closeWriteChannel();
    if (!microsoft.waitForFinished(1000)) {
      microsoft.kill();
      microsoft.waitForFinished(1000);
    }
  }
  void startMicrosoft() {
    if (microsoft.state() != QProcess::NotRunning)
      return;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert("WINEPREFIX", microsoftRoot() + "/prefix");
    environment.insert("WINEDEBUG", "-all");
    environment.insert("WINEDLLOVERRIDES",
                       "winemenubuilder.exe=d;sapi,msttsengine,msttsengine_"
                       "onecore,msvcp110_win=n,b");
    microsoft.setProcessEnvironment(environment);
    microsoftBuffer.clear();
    microsoft.start("wine",
                    {microsoftRoot() + "/speech11-bridge.exe", "--server"});
  }
  void speakMicrosoft(const QString &input, const QString &mode, int speed) {
    startMicrosoft();
    microsoftPending = true;
    QByteArray packet;
    auto number = [&](quint32 value) {
      char bytes[4];
      qToLittleEndian<quint32>(value, reinterpret_cast<uchar *>(bytes));
      packet.append(bytes, 4);
    };
    for (const auto &value : {mode == "sapi-paulina" ? QByteArray("Paulina")
                                                     : QByteArray("ZiraPro"),
                              input.toUtf8()}) {
      number(value.size());
      packet.append(value);
    }
    number(static_cast<quint32>(qBound(-10, qRound((speed - 150) / 30.0), 10)));
    microsoft.write(packet);
  }
  void configureLayer(QWidget &widget, LayerShellQt::Window *&layer,
                      bool input) {
    if (!KWindowSystem::isPlatformWayland())
      return;
    widget.winId();
    layer = LayerShellQt::Window::get(widget.windowHandle());
    layer->setLayer(LayerShellQt::Window::LayerOverlay);
    layer->setAnchors(LayerShellQt::Window::AnchorTop);
    layer->setMargins(QMargins(0, 40, 0, 0));
    layer->setExclusiveZone(-1);
    layer->setWantsToBeOnActiveScreen(true);
    layer->setScope("vctts-overlay");
    layer->setKeyboardInteractivity(
        input ? LayerShellQt::Window::KeyboardInteractivityExclusive
              : LayerShellQt::Window::KeyboardInteractivityNone);
    layer->setActivateOnShow(input);
  }
  void positionOverlay(QWidget &widget) {
    auto screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen)
      screen = QGuiApplication::primaryScreen();
    auto area = screen->availableGeometry();
    widget.setFixedWidth(qMin(720, area.width() - 48));
    if (!KWindowSystem::isPlatformWayland())
      widget.move(area.center().x() - widget.width() / 2, area.top() + 40);
  }
  QAction *shortcut(QFormLayout *layout, const QString &id,
                    const QString &label, const QString &defaultKey) {
    auto action = new QAction(this);
    action->setObjectName(id);
    action->setText("VCTTS " + label);
    KGlobalAccel::self()->setShortcut(
        action, {QKeySequence(
                    settings.value("shortcut/" + id, defaultKey).toString())});
    auto editor = new QKeySequenceEdit;
    auto keys = KGlobalAccel::self()->shortcut(action);
    if (!keys.isEmpty())
      editor->setKeySequence(keys.first());
    layout->addRow(label + " shortcut", editor);
    connect(editor, &QKeySequenceEdit::editingFinished, this,
            [this, editor, action, id] {
              auto key = editor->keySequence();
              if (KGlobalAccel::self()->setShortcut(
                      action, {key}, KGlobalAccel::NoAutoloading))
                settings.setValue("shortcut/" + id, key.toString());
              else
                status->setText(
                    "Shortcut is already assigned to another application");
            });
    return action;
  }
  void restartInputTimeout() {
    int seconds = settings.value("timeout", 30).toInt();
    if (seconds > 0)
      inputTimeout.start(seconds * 1000);
    else
      inputTimeout.stop();
  }
  void cancelInput() {
    inputTimeout.stop();
    quick.hide();
    quickText->clear();
  }
  void commitInput() {
    auto message = quickText->text();
    cancelInput();
    speak(message);
  }
  void toggleInput() {
    if (quick.isVisible()) {
      commitInput();
      return;
    }
    playbackOverlay.hide();
    positionOverlay(quick);
    quick.show();
    quick.raise();
    if (!KWindowSystem::isPlatformWayland())
      KWindowSystem::activateWindow(quick.windowHandle());
    quickText->setFocus();
    restartInputTimeout();
  }
  void showSettings() {
    cancelInput();
    showNormal();
    raise();
    KWindowSystem::activateWindow(windowHandle());
  }
  void closeEvent(QCloseEvent *event) override {
    if (tray->isVisible()) {
      hide();
      event->ignore();
    } else
      QWidget::closeEvent(event);
  }
  void refreshHistory() {
    historyMenu->clear();
    for (const auto &message : history) {
      auto action = historyMenu->addAction(message.left(70));
      connect(action, &QAction::triggered, this,
              [this, message] { speak(message); });
    }
    historyMenu->setEnabled(!history.isEmpty());
  }
  void showProgress(const QString &message) {
    if (quick.isVisible())
      return;
    positionOverlay(playbackOverlay);
    overlayStatus->setText(message);
    playbackOverlay.show();
  }
  void setupAudio() {
    QProcess p;
    p.start(QCoreApplication::applicationDirPath() + "/vctts-audio");
    if (!p.waitForFinished(5000)) {
      p.kill();
      status->setText("Virtual audio setup timed out");
      return;
    }
    status->setText(p.exitCode() == 0
                        ? "Virtual microphone ready"
                        : QString::fromUtf8(p.readAllStandardError()));
    refreshDevices();
  }
  void refreshDevices() {
    auto one = first->currentData().toByteArray(),
         two = second->currentData().toByteArray();
    if (one.isEmpty())
      one = settings.value("first").toByteArray();
    if (two.isEmpty())
      two = settings.value("second").toByteArray();
    first->clear();
    second->clear();
    first->addItem("System default", QByteArray("default"));
    second->addItem("Disabled", QByteArray("none"));
    for (const auto &device : QMediaDevices::audioOutputs()) {
      first->addItem(device.description(), device.id());
      second->addItem(device.description(), device.id());
    }
    first->setCurrentIndex(qMax(0, first->findData(one)));
    int index = second->findData(two);
    second->setCurrentIndex(qMax(0, index));
  }
  QAudioDevice device(const QByteArray &id) {
    for (const auto &entry : QMediaDevices::audioOutputs())
      if (entry.id() == id)
        return entry;
    return QMediaDevices::defaultAudioOutput();
  }
  void stopSpeech() {
    if (microsoftPending) {
      microsoftPending = false;
      microsoft.kill();
      microsoft.waitForFinished(1000);
      microsoftBuffer.clear();
    }
    pendingMessage.clear();
    if (workerCancel)
      workerCancel->store(true);
    playbackOverlay.hide();
    ++generation;
    cancelled = true;
    if (reply) {
      reply->abort();
      reply = nullptr;
    }
    for (auto &player : players)
      player.stop();
    if (!synthesis.isRunning())
      speakButton->setEnabled(true);
  }
  void speak(const QString &input, bool translated = false) {
    if (input.trimmed().isEmpty())
      return;
    if (synthesis.isRunning()) {
      stopSpeech();
      pendingMessage = input;
      status->setText("Replacing pending speech…");
      return;
    }
    stopSpeech();
    cancelled = false;
    currentMessage = input;
    auto mode = voice->currentData().toString();
    auto path = pack->text();
    auto command = custom->text();
    int speed = rate->value();
    showProgress("Generating speech…");
    status->setText("Generating speech…");
    speakButton->setEnabled(false);
    if (translate->isChecked() && !translated) {
      QUrl url("https://translate.googleapis.com/translate_a/single");
      QUrlQuery query;
      query.addQueryItem("client", "gtx");
      query.addQueryItem("sl", "auto");
      query.addQueryItem("tl", language->currentData().toString());
      query.addQueryItem("dt", "t");
      query.addQueryItem("q", input);
      url.setQuery(query);
      QNetworkRequest request(url);
      request.setTransferTimeout(30000);
      auto current = network.get(request);
      reply = current;
      auto id = generation;
      status->setText("Translating…");
      connect(current, &QNetworkReply::finished, this, [this, current, id] {
        if (reply == current)
          reply = nullptr;
        if (id == generation) {
          speakButton->setEnabled(true);
          QString result;
          if (current->error() == QNetworkReply::NoError) {
            auto rows = QJsonDocument::fromJson(current->readAll()).array();
            if (!rows.isEmpty())
              for (const auto &entry : rows[0].toArray()) {
                auto row = entry.toArray();
                if (!row.isEmpty())
                  result += row[0].toString();
              }
          }
          if (result.isEmpty()) {
            status->setText(
                "Translation failed; original text was not sent to speech");
            showProgress(status->text());
            QTimer::singleShot(5000, &playbackOverlay, &QWidget::hide);
          } else
            speak(result, true);
        }
        current->deleteLater();
      });
      return;
    }
    if (translated && (mode == "en-us" || mode == "pl"))
      mode = language->currentData().toString();
    if (mode == "online") {
      speakOnline(input);
      return;
    }
    if (mode.startsWith("sapi-")) {
      if (translated)
        mode = language->currentData() == "pl" ? "sapi-paulina" : "sapi-zira";
      speakMicrosoft(input, mode, speed);
      return;
    }
    workerCancel = std::make_shared<std::atomic_bool>(false);
    auto cancel = workerCancel;
    synthesis.setFuture(QtConcurrent::run([input, mode, path, command, speed,
                                           cancel]() -> Speech {
      if (mode == "phonomenal") {
        tts_phonomenal::select(path.toStdString());
        auto data = tts_phonomenal::speak(input.toStdString());
        if (data.empty())
          return {{}, QString::fromStdString(tts_phonomenal::last_error())};
        return {QByteArray(reinterpret_cast<const char *>(data.data()),
                           data.size()),
                {}};
      }
      QString program = "espeak-ng";
      QStringList args = {"--stdout", "--stdin", "-v",
                          mode,       "-s",      QString::number(speed)};
      if (mode == "custom") {
        args = QProcess::splitCommand(command);
        if (args.isEmpty())
          return {{}, "Set a custom TTS program first"};
        program = args.takeFirst();
      }
      QProcess process;
      process.start(program, args);
      if (!process.waitForStarted(5000))
        return {{}, "Could not start TTS program"};
      process.write(input.toUtf8());
      process.closeWriteChannel();
      QElapsedTimer elapsed;
      elapsed.start();
      while (!process.waitForFinished(50)) {
        if (cancel->load() || elapsed.elapsed() > 30000) {
          process.kill();
          process.waitForFinished();
          return {{},
                  cancel->load() ? "Cancelled" : "TTS generation timed out"};
        }
      }
      if (process.exitCode() != 0)
        return {{}, "TTS program failed"};
      auto data = process.readAllStandardOutput();
      if (data.size() < 44)
        return {{}, "TTS program returned no audio"};
      return {data, {}};
    }));
  }
  void speakOnline(const QString &input) {
    showProgress("Generating online speech…");
    QNetworkRequest request(
        QUrl("https://api.streamelements.com/kappa/v2/speech"));
    request.setHeader(QNetworkRequest::ContentTypeHeader,
                      "application/x-www-form-urlencoded");
    request.setTransferTimeout(30000);
    QUrlQuery query;
    query.addQueryItem("voice", "Brian");
    query.addQueryItem("text", input);
    auto current =
        network.post(request, query.query(QUrl::FullyEncoded).toUtf8());
    reply = current;
    auto id = generation;
    connect(current, &QNetworkReply::finished, this, [this, current, id] {
      if (reply == current)
        reply = nullptr;
      if (id == generation) {
        speakButton->setEnabled(true);
        if (current->error() == QNetworkReply::NoError)
          play(current->readAll());
        else {
          status->setText("Online TTS failed: " + current->errorString());
          showProgress(status->text());
          QTimer::singleShot(5000, &playbackOverlay, &QWidget::hide);
        }
      }
      current->deleteLater();
    });
  }
  void play(const QByteArray &audio) {
    if (audio.isEmpty()) {
      status->setText("No audio returned");
      return;
    }
    for (auto &player : players) {
      player.stop();
      player.setSource(QUrl());
    }
    for (const auto &old : QDir(temporary.path()).entryList(QDir::Files))
      QFile::remove(temporary.filePath(old));
    QString file =
        temporary.filePath("speech-" + QString::number(generation) +
                           (audio.startsWith("RIFF") ? ".wav" : ".mp3"));
    auto normalized = audio;
    // eSpeak's stdout WAV uses streaming length placeholders; finalize them for
    // seeking.
    if (normalized.size() >= 44 && normalized.startsWith("RIFF") &&
        normalized.mid(8, 4) == "WAVE") {
      qToLittleEndian<quint32>(
          normalized.size() - 8,
          reinterpret_cast<uchar *>(normalized.data() + 4));
      for (qsizetype pos = 12; pos + 8 <= normalized.size();) {
        auto length = qFromLittleEndian<quint32>(
            reinterpret_cast<const uchar *>(normalized.constData() + pos + 4));
        if (normalized.mid(pos, 4) == "data") {
          qToLittleEndian<quint32>(
              normalized.size() - pos - 8,
              reinterpret_cast<uchar *>(normalized.data() + pos + 4));
          break;
        }
        if (length > normalized.size() - pos - 8)
          break;
        pos += 8 + length + (length % 2);
      }
    }
    QFile output(file);
    if (!output.open(QIODevice::WriteOnly) ||
        output.write(normalized) != normalized.size()) {
      status->setText("Could not store temporary speech audio");
      return;
    }
    output.close();
    outputs[0].setDevice(device(first->currentData().toByteArray()));
    players[0].setSource(QUrl::fromLocalFile(file));
    players[0].play();
    auto destination = second->currentData().toByteArray();
    if (destination != "none" &&
        device(destination).id() != outputs[0].device().id()) {
      outputs[1].setDevice(device(destination));
      players[1].setSource(QUrl::fromLocalFile(file));
      players[1].play();
    }
    if (sendChat->isChecked()) {
      QAudioDevice bus;
      for (const auto &entry : QMediaDevices::audioOutputs())
        if (entry.id() == "vctts_bus" ||
            entry.description().contains("VCTTS_Chat_Audio") ||
            entry.description().contains("VCTTS Chat Audio")) {
          bus = entry;
          break;
        }
      if (bus.isNull()) {
        status->setText("Virtual microphone output missing — use Advanced "
                        "settings to repair it");
        showProgress(status->text());
        return;
      }
      bool already =
          outputs[0].device().id() == bus.id() ||
          (destination != "none" && device(destination).id() == bus.id());
      if (!already) {
        outputs[2].setDevice(bus);
        outputs[2].setVolume(1.0);
        players[2].setSource(QUrl::fromLocalFile(file));
        players[2].play();
      }
    }
    status->setText(sendChat->isChecked()
                        ? "Playing · speech sent to VCTTS Virtual Microphone"
                        : "Playing · virtual microphone disabled");
    showProgress("Speaking · " + currentMessage.left(180));
    history.removeAll(currentMessage);
    history.prepend(currentMessage);
    while (history.size() > 10)
      history.removeLast();
    refreshHistory();
  }
  void updateBridgeGain() {
    if (bridgeModule.isEmpty())
      return;
    QProcess list;
    list.start("pactl", {"-f", "json", "list", "sink-inputs"});
    if (!list.waitForFinished(3000))
      return;
    for (const auto &entry :
         QJsonDocument::fromJson(list.readAllStandardOutput()).array()) {
      auto object = entry.toObject();
      if (object["owner_module"].toVariant().toString() == bridgeModule) {
        QProcess volume;
        volume.start("pactl", {"set-sink-input-volume",
                               QString::number(object["index"].toInteger()),
                               QString::number(micGain->value()) + "%"});
        volume.waitForFinished(3000);
      }
    }
  }
  void setBridge(bool enabled) {
    if (!bridgeModule.isEmpty()) {
      QProcess p;
      p.start("pactl", {"unload-module", bridgeModule});
      p.waitForFinished(3000);
      bridgeModule.clear();
    }
    if (!enabled || micMuted->isChecked()) {
      status->setText(
          enabled ? "Microphone muted · TTS remains available"
                  : "Virtual microphone ready · physical microphone off");
      return;
    }
    QProcess source;
    source.start("pactl", {"get-default-source"});
    source.waitForFinished(3000);
    auto name = microphone->currentData().toString();
    if (name == "default")
      name = QString::fromUtf8(source.readAllStandardOutput()).trimmed();
    if (name.isEmpty() || name.contains("vctts") || name.endsWith(".monitor")) {
      status->setText(
          "Choose a real default microphone before enabling the bridge");
      bridge->setChecked(false);
      return;
    }
    QProcess p;
    p.start("pactl", {"load-module", "module-loopback", "source=" + name,
                      "sink=vctts_bus", "latency_msec=30",
                      "source_dont_move=true", "sink_dont_move=true"});
    p.waitForFinished(3000);
    if (p.exitCode() == 0) {
      bridgeModule = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
      updateBridgeGain();
      QTimer::singleShot(150, this, [this] { updateBridgeGain(); });
      status->setText("Real microphone mixed into VCTTS Virtual Microphone");
    } else {
      status->setText("Could not enable microphone bridge");
      bridge->setChecked(false);
    }
  }
};
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setQuitOnLastWindowClosed(false);
  app.setOrganizationName("aridlin");
  app.setApplicationName("VCTTS");
  app.setDesktopFileName("vctts");
  QLocalSocket existing;
  const QString instance = app.arguments().filter("--test-").isEmpty()
                               ? "aridlin-vctts"
                               : "aridlin-vctts-test";
  existing.connectToServer(instance);
  if (existing.waitForConnected(200)) {
    existing.write("settings");
    existing.waitForBytesWritten();
    return 0;
  }
  QLocalServer server;
  QLocalServer::removeServer(instance);
  server.listen(instance);
  Window window;
  QObject::connect(&server, &QLocalServer::newConnection, &window, [&] {
    auto socket = server.nextPendingConnection();
    window.showSettings();
    socket->deleteLater();
  });
  if (app.arguments().contains("--test-cancel"))
    QTimer::singleShot(500, &window, [&] { window.cancellationTest(); });
  else if (app.arguments().contains("--test-overlay"))
    QTimer::singleShot(500, &window, [&] { window.overlayTest(); });
  else if (app.arguments().contains("--test-configured-audio"))
    QTimer::singleShot(1000, &window, [&] { window.selfTest(true); });
  else if (app.arguments().contains("--test-audio"))
    QTimer::singleShot(1000, &window, [&] { window.selfTest(); });
  else if (app.arguments().contains("--preview-overlay")) {
    QTimer::singleShot(500, &window, [&] { window.toggleInput(); });
  } else if (!app.arguments().contains("--background"))
    window.show();
  return app.exec();
}
