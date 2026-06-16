#include "settingsdialog.h"
#include "appconfig.h"
#include "dictionaryconnector.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QMessageBox>
#include <QListWidget>
#include <QStackedWidget>
#include <QFrame>
#include <QScrollArea>
#include <QSizePolicy>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setObjectName("settingsDialog");
    setWindowTitle(tr("Settings"));
    setModal(true);

    buildUI();
    loadFromConfig();
}

void SettingsDialog::buildUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // 主体：左侧分类列表 + 右侧堆叠面板
    QHBoxLayout* bodyLayout = new QHBoxLayout();
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    bodyLayout->setSpacing(0);

    QListWidget* categoryList = new QListWidget(this);
    categoryList->setObjectName("settingsCategoryList");
    categoryList->setFixedWidth(176);
    categoryList->setFrameShape(QFrame::NoFrame);

    QStackedWidget* pages = new QStackedWidget(this);
    pages->setObjectName("settingsPages");

    // 新建一个分类页：顶部标题+副标题，下面竖排放分组卡片；返回承载卡片的布局。
    // 外层用滚动区承载，避免系统字体较大或翻译较长时内容被窗口裁掉。
    auto addPage = [&](const QString& title, const QString& subtitle) -> QVBoxLayout* {
        QScrollArea* scroll = new QScrollArea(this);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidgetResizable(true);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

        QWidget* page = new QWidget(scroll);
        page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        QVBoxLayout* outer = new QVBoxLayout(page);
        outer->setContentsMargins(26, 24, 26, 24);
        outer->setSpacing(16);

        QVBoxLayout* header = new QVBoxLayout();
        header->setSpacing(3);
        QLabel* t = new QLabel(title, page);
        t->setProperty("role", "pageTitle");
        t->setWordWrap(true);
        header->addWidget(t);
        if (!subtitle.isEmpty()) {
            QLabel* s = new QLabel(subtitle, page);
            s->setProperty("role", "pageSubtitle");
            s->setWordWrap(true);
            header->addWidget(s);
        }
        outer->addLayout(header);

        scroll->setWidget(page);
        pages->addWidget(scroll);
        new QListWidgetItem(title, categoryList);
        return outer;
    };

    // 在页面里加一个分组：可选小标题 + 卡片容器；返回卡片内部布局供加行。
    auto addSection = [&](QVBoxLayout* pageLayout, const QString& sectionTitle) -> QVBoxLayout* {
        if (!sectionTitle.isEmpty()) {
            QLabel* st = new QLabel(sectionTitle, this);
            st->setProperty("role", "sectionTitle");
            st->setWordWrap(true);
            pageLayout->addWidget(st);
        }
        QFrame* card = new QFrame(this);
        card->setObjectName("settingsSection");
        QVBoxLayout* inner = new QVBoxLayout(card);
        inner->setContentsMargins(16, 4, 16, 4);
        inner->setSpacing(0);
        pageLayout->addWidget(card);
        return inner;
    };

    // 行间细分隔线：卡片里已有内容时，在新行前补一条 1px 线。
    auto addSeparatorIfNeeded = [&](QVBoxLayout* card) {
        if (card->count() > 0) {
            QFrame* sep = new QFrame(this);
            sep->setObjectName("settingsRowSeparator");
            sep->setFixedHeight(1);
            card->addWidget(sep);
        }
    };

    // 标准行：左标签 + 右控件（右对齐），下方可附一句灰色说明。
    auto addRow = [&](QVBoxLayout* card, const QString& label,
                      QWidget* control, const QString& hint) {
        addSeparatorIfNeeded(card);
        QWidget* row = new QWidget(this);
        QGridLayout* g = new QGridLayout(row);
        g->setContentsMargins(0, 12, 0, 12);
        g->setHorizontalSpacing(12);
        g->setVerticalSpacing(4);
        QLabel* l = new QLabel(label, row);
        l->setProperty("role", "rowLabel");
        l->setWordWrap(true);
        l->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        g->addWidget(l, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);
        if (control) {
            control->setSizePolicy(QSizePolicy::Fixed, control->sizePolicy().verticalPolicy());
            g->addWidget(control, 0, 1, Qt::AlignRight | Qt::AlignVCenter);
        }
        g->setColumnStretch(0, 1);
        g->setColumnStretch(1, 0);
        if (!hint.isEmpty()) {
            QLabel* h = new QLabel(hint, row);
            h->setProperty("role", "caption");
            h->setWordWrap(true);
            h->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            g->addWidget(h, 1, 0, 1, 2);
        }
        card->addWidget(row);
    };

    // 勾选行：复选框自带文案，下方说明缩进对齐到文字。
    auto addCheckRow = [&](QVBoxLayout* card, QCheckBox* cb, const QString& hint) {
        addSeparatorIfNeeded(card);
        QWidget* row = new QWidget(this);
        QVBoxLayout* v = new QVBoxLayout(row);
        v->setContentsMargins(0, 12, 0, 12);
        v->setSpacing(4);
        cb->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        v->addWidget(cb);
        if (!hint.isEmpty()) {
            QLabel* h = new QLabel(hint, row);
            h->setProperty("role", "caption");
            h->setWordWrap(true);
            h->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            h->setContentsMargins(26, 0, 0, 0);
            v->addWidget(h);
        }
        card->addWidget(row);
    };

    // 宽行：标签在上、控件占满整行（用于命令输入这种长控件），说明在下。
    auto addWideRow = [&](QVBoxLayout* card, const QString& label,
                          QWidget* control, const QString& hint) {
        addSeparatorIfNeeded(card);
        QWidget* row = new QWidget(this);
        QVBoxLayout* v = new QVBoxLayout(row);
        v->setContentsMargins(0, 12, 0, 12);
        v->setSpacing(6);
        QLabel* l = new QLabel(label, row);
        l->setProperty("role", "rowLabel");
        l->setWordWrap(true);
        v->addWidget(l);
        v->addWidget(control);
        if (!hint.isEmpty()) {
            QLabel* h = new QLabel(hint, row);
            h->setProperty("role", "caption");
            h->setWordWrap(true);
            h->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            v->addWidget(h);
        }
        card->addWidget(row);
    };

    const int kFieldWidth = 132;  // 数值类控件统一宽度，避免被拉满整行

    // ========== 常规 ==========
    QVBoxLayout* generalPage = addPage(
        tr("General"),
        tr("Startup and overall app behavior."));
    {
        QVBoxLayout* sec = addSection(generalPage, QString());
        m_rememberLastFile = new QCheckBox(tr("Reopen last session on startup"), this);
        addCheckRow(sec, m_rememberLastFile,
            tr("Automatically reopen the documents you had open when you last quit."));
    }
    generalPage->addStretch(1);

    // ========== 阅读体验（缓存 + 性能）==========
    QVBoxLayout* readingPage = addPage(
        tr("Reading"),
        tr("Tune how pages are cached and how smoothly the view responds."));
    {
        QVBoxLayout* cacheSec = addSection(readingPage, tr("Page Cache"));
        m_maxCacheSize = new QSpinBox(this);
        m_maxCacheSize->setRange(1, 100);
        m_maxCacheSize->setSuffix(tr(" pages"));
        m_maxCacheSize->setFixedWidth(kFieldWidth);
        addRow(cacheSec, tr("Page cache limit"), m_maxCacheSize,
            tr("How many rendered pages to keep in memory. Higher is smoother when "
               "flipping back and forth, but uses more memory. Recommended: 10."));

        m_preloadMargin = new QSpinBox(this);
        m_preloadMargin->setRange(0, 2000);
        m_preloadMargin->setSingleStep(50);
        m_preloadMargin->setSuffix(tr(" px"));
        m_preloadMargin->setFixedWidth(kFieldWidth);
        addRow(cacheSec, tr("Preload distance"), m_preloadMargin,
            tr("How far beyond the screen to render pages ahead of time, so scrolling "
               "feels instant. Recommended: 500."));

        QVBoxLayout* perfSec = addSection(readingPage, tr("Performance"));
        m_resizeDebounce = new QSpinBox(this);
        m_resizeDebounce->setRange(0, 1000);
        m_resizeDebounce->setSingleStep(10);
        m_resizeDebounce->setSuffix(tr(" ms"));
        m_resizeDebounce->setFixedWidth(kFieldWidth);
        addRow(perfSec, tr("Resize delay"), m_resizeDebounce,
            tr("How long to wait after you stop resizing the window before re-rendering. "
               "Larger values feel calmer; smaller values update faster. Recommended: 150."));
    }
    readingPage->addStretch(1);

    // ========== 取词（OCR + 词典）==========
    QPushButton* testDictBtn = nullptr;
    QVBoxLayout* lookupPage = addPage(
        tr("Word Lookup"),
        tr("Recognize text under the cursor and look it up in an external dictionary."));
    {
        QVBoxLayout* ocrSec = addSection(lookupPage, tr("Hover Recognition (OCR)"));
        m_ocrDebounce = new QSpinBox(this);
        m_ocrDebounce->setRange(0, 2000);
        m_ocrDebounce->setSingleStep(50);
        m_ocrDebounce->setSuffix(tr(" ms"));
        m_ocrDebounce->setFixedWidth(kFieldWidth);
        addRow(ocrSec, tr("Hover delay"), m_ocrDebounce,
            tr("How long the cursor must rest before a word is recognized. Larger values "
               "avoid accidental lookups. Recommended: 300."));

        m_ocrRegionSize = new QSpinBox(this);
        m_ocrRegionSize->setRange(50, 600);
        m_ocrRegionSize->setSingleStep(20);
        m_ocrRegionSize->setSuffix(tr(" px"));
        m_ocrRegionSize->setFixedWidth(kFieldWidth);
        addRow(ocrSec, tr("Capture size"), m_ocrRegionSize,
            tr("The size of the area around the cursor sent to OCR. Larger captures more "
               "context but is slower. Recommended: 200."));

        QVBoxLayout* dictSec = addSection(lookupPage, tr("Dictionary"));
        QWidget* dictRow = new QWidget(this);
        QHBoxLayout* dictCmdRow = new QHBoxLayout(dictRow);
        dictCmdRow->setContentsMargins(0, 0, 0, 0);
        m_dictionaryCommand = new QLineEdit(this);
        m_dictionaryCommand->setPlaceholderText(tr("e.g. open -a GoldenDict {word}"));
        testDictBtn = new QPushButton(tr("Test"), this);
        dictCmdRow->addWidget(m_dictionaryCommand, 1);
        dictCmdRow->addWidget(testDictBtn);
        addWideRow(dictSec, tr("Lookup command"), dictRow,
            tr("Shell command used to look up the recognized word. Use {word} as a "
               "placeholder; if omitted, the word is appended to the command."));
    }
    lookupPage->addStretch(1);

    connect(testDictBtn, &QPushButton::clicked, this, [this]() {
        const QString cmd = m_dictionaryCommand->text().trimmed();
        if (cmd.isEmpty()) {
            QMessageBox::information(this, tr("Test Dictionary"),
                                     tr("Please enter a dictionary command first."));
            return;
        }
        QString error;
        // 用一个测试词实际调用，词典 UI 弹出即表示打通
        bool ok = DictionaryConnector::runCommand(cmd, QStringLiteral("test"), &error);
        if (ok) {
            QMessageBox::information(this, tr("Test Dictionary"),
                tr("Command launched. If your dictionary popped up with \"test\", it works."));
        } else {
            QMessageBox::warning(this, tr("Test Dictionary"),
                tr("Failed to run the command:\n%1").arg(error));
        }
    });

    bodyLayout->addWidget(categoryList);
    bodyLayout->addWidget(pages, 1);
    mainLayout->addLayout(bodyLayout, 1);

    // 底部按钮区
    QDialogButtonBox* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::RestoreDefaults,
        this);
    QHBoxLayout* buttonRow = new QHBoxLayout();
    buttonRow->setContentsMargins(20, 12, 20, 16);
    buttonRow->addWidget(buttons);
    mainLayout->addLayout(buttonRow);

    connect(buttons, &QDialogButtonBox::accepted, this, [this]() {
        applyToConfig();
        accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked,
            this, &SettingsDialog::restoreDefaults);

    // 左右联动：选分类切换右侧页面
    connect(categoryList, &QListWidget::currentRowChanged,
            pages, &QStackedWidget::setCurrentIndex);
    categoryList->setCurrentRow(0);

    setMinimumSize(760, 540);
    resize(820, 600);
}

void SettingsDialog::loadFromConfig()
{
    AppConfig& cfg = AppConfig::instance();
    m_rememberLastFile->setChecked(cfg.rememberLastFile());
    m_maxCacheSize->setValue(cfg.maxCacheSize());
    m_preloadMargin->setValue(cfg.preloadMargin());
    m_resizeDebounce->setValue(cfg.resizeDebounceDelay());
    m_ocrDebounce->setValue(cfg.ocrDebounceDelay());
    m_ocrRegionSize->setValue(cfg.ocrHoverRegionSize());
    m_dictionaryCommand->setText(cfg.dictionaryCommand());
}

void SettingsDialog::applyToConfig()
{
    AppConfig& cfg = AppConfig::instance();
    cfg.setRememberLastFile(m_rememberLastFile->isChecked());
    cfg.setMaxCacheSize(m_maxCacheSize->value());
    cfg.setPreloadMargin(m_preloadMargin->value());
    cfg.setResizeDebounceDelay(m_resizeDebounce->value());
    cfg.setOcrDebounceDelay(m_ocrDebounce->value());
    cfg.setOcrHoverRegionSize(m_ocrRegionSize->value());
    cfg.setDictionaryCommand(m_dictionaryCommand->text().trimmed());
    cfg.save();
}

void SettingsDialog::restoreDefaults()
{
    // 重置会立即落盘且不可撤销，先二次确认，避免误点
    const auto choice = QMessageBox::question(
        this, tr("Restore Defaults"),
        tr("Reset all settings to their default values? This cannot be undone."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes)
        return;

    // resetToDefaults 会立即落盘（只重置偏好项，不动会话/文件关联）
    AppConfig::instance().resetToDefaults();
    loadFromConfig();
}
