#include "outlinedialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QLabel>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QPushButton>

OutlineDialog::OutlineDialog(Mode mode, int maxPage, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_maxPage(maxPage)
    , m_titleEdit(nullptr)
    , m_pageSpinBox(nullptr)
    , m_buttonBox(nullptr)
{
    setupUI();
    applyStyleSheet();

    // 设置窗口属性
    setModal(true);
    setMinimumWidth(400);

    // 连接信号
    connect(m_buttonBox, &QDialogButtonBox::accepted,
            this, &OutlineDialog::onAccepted);
    connect(m_buttonBox, &QDialogButtonBox::rejected,
            this, &QDialog::reject);
}

OutlineDialog::~OutlineDialog()
{
}

void OutlineDialog::setupUI()
{
    // 设置标题
    if (m_mode == AddMode) {
        setWindowTitle(tr("添加大纲"));
    } else {
        setWindowTitle(tr("编辑大纲"));
    }

    // 主布局
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(16);
    mainLayout->setContentsMargins(20, 20, 20, 20);

    // 说明文字
    QLabel* descLabel = new QLabel(this);
    if (m_mode == AddMode) {
        descLabel->setText(tr("请输入大纲项信息："));
    } else {
        descLabel->setText(tr("编辑大纲项信息："));
    }
    QFont descFont = descLabel->font();
    descFont.setPointSize(10);
    descLabel->setFont(descFont);
    descLabel->setStyleSheet("color: #666666;");
    mainLayout->addWidget(descLabel);

    // 表单布局
    QFormLayout* formLayout = new QFormLayout();
    formLayout->setSpacing(12);
    formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    formLayout->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);

    // 标题输入
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setPlaceholderText(tr("输入大纲标题"));
    m_titleEdit->setMinimumWidth(300);
    QLabel* titleLabel = new QLabel(tr("标题:"), this);
    titleLabel->setMinimumWidth(60);
    formLayout->addRow(titleLabel, m_titleEdit);

    // 页码选择
    m_pageSpinBox = new QSpinBox(this);
    m_pageSpinBox->setMinimum(1);
    m_pageSpinBox->setMaximum(m_maxPage);
    m_pageSpinBox->setValue(1);
    m_pageSpinBox->setSuffix(tr(" 页"));
    m_pageSpinBox->setMinimumWidth(150);
    QLabel* pageLabel = new QLabel(tr("目标页码:"), this);
    pageLabel->setMinimumWidth(60);
    formLayout->addRow(pageLabel, m_pageSpinBox);

    mainLayout->addLayout(formLayout);

    // 添加弹性空间
    mainLayout->addStretch();

    // 按钮栏
    m_buttonBox = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel,
        this);

    if (m_mode == AddMode) {
        m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("添加"));
    } else {
        m_buttonBox->button(QDialogButtonBox::Ok)->setText(tr("保存"));
    }
    m_buttonBox->button(QDialogButtonBox::Cancel)->setText(tr("取消"));

    mainLayout->addWidget(m_buttonBox);

    // 设置Tab顺序
    setTabOrder(m_titleEdit, m_pageSpinBox);
    setTabOrder(m_pageSpinBox, m_buttonBox);

    // 焦点设置
    m_titleEdit->setFocus();
}

void OutlineDialog::applyStyleSheet()
{
    QString style = R"(
        QDialog {
            background-color: #FFFFFF;
        }

        QLineEdit {
            padding: 8px 12px;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            font-size: 10pt;
            background-color: #FFFFFF;
        }

        QLineEdit:focus {
            border-color: #3B82F6;
            outline: none;
        }

        QLineEdit:hover {
            border-color: #9CA3AF;
        }

        QSpinBox {
            padding: 8px 12px;
            border: 1px solid #D1D5DB;
            border-radius: 4px;
            font-size: 10pt;
            background-color: #FFFFFF;
        }

        QSpinBox:focus {
            border-color: #3B82F6;
        }

        QSpinBox:hover {
            border-color: #9CA3AF;
        }

        QSpinBox::up-button, QSpinBox::down-button {
            background-color: #F3F4F6;
            border: none;
            width: 20px;
        }

        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: #E5E7EB;
        }

        QPushButton {
            padding: 8px 24px;
            border: none;
            border-radius: 4px;
            font-size: 10pt;
            font-weight: bold;
            min-width: 80px;
        }

        QPushButton:hover {
            opacity: 0.9;
        }

        QPushButton:pressed {
            opacity: 0.8;
        }

        QDialogButtonBox QPushButton:default {
            background-color: #3B82F6;
            color: #FFFFFF;
        }

        QDialogButtonBox QPushButton:default:hover {
            background-color: #2563EB;
        }

        QDialogButtonBox QPushButton:!default {
            background-color: #F3F4F6;
            color: #374151;
        }

        QDialogButtonBox QPushButton:!default:hover {
            background-color: #E5E7EB;
        }
    )";

    setStyleSheet(style);
}

void OutlineDialog::setTitle(const QString& title)
{
    m_titleEdit->setText(title);
}

QString OutlineDialog::title() const
{
    return m_titleEdit->text().trimmed();
}

void OutlineDialog::setPageIndex(int pageIndex)
{
    // 转换为1-based显示
    m_pageSpinBox->setValue(pageIndex + 1);
}

int OutlineDialog::pageIndex() const
{
    // 转换为0-based
    return m_pageSpinBox->value() - 1;
}

bool OutlineDialog::validate()
{
    QString title = m_titleEdit->text().trimmed();

    if (title.isEmpty()) {
        QMessageBox::warning(this, tr("输入错误"),
                             tr("大纲标题不能为空！"));
        m_titleEdit->setFocus();
        return false;
    }

    if (title.length() > 200) {
        QMessageBox::warning(this, tr("输入错误"),
                             tr("大纲标题过长（最多200字符）！"));
        m_titleEdit->setFocus();
        return false;
    }

    return true;
}

void OutlineDialog::onAccepted()
{
    if (validate()) {
        accept();
    }
}
