#include "chinesetokenizer.h"
#include "rapidocr-cpp/utils.h"
#include <QFileInfo>
#include <QDebug>
#include <cmath>

ChineseTokenizer& ChineseTokenizer::instance()
{
    static ChineseTokenizer instance;
    return instance;
}

ChineseTokenizer::ChineseTokenizer()
    : m_initialized(false)
{
}

ChineseTokenizer::~ChineseTokenizer()
{
}

bool ChineseTokenizer::initialize(const QString& dictDir)
{
    if (m_initialized) {
        qInfo() << "ChineseTokenizer already initialized";
        return true;
    }

    m_dictDir = dictDir;

    // 检查必要的词典文件
    QString dictPath = dictDir + "/jieba.dict.utf8";
    QString hmmPath = dictDir + "/hmm_model.utf8";
    QString userDictPath = dictDir + "/user.dict.utf8";
    QString idfPath = dictDir + "/idf.utf8";
    QString stopWordsPath = dictDir + "/stop_words.utf8";

    QStringList missingFiles;
    if (!QFileInfo::exists(dictPath)) missingFiles << "jieba.dict.utf8";
    if (!QFileInfo::exists(hmmPath)) missingFiles << "hmm_model.utf8";
    if (!QFileInfo::exists(userDictPath)) missingFiles << "user.dict.utf8";
    if (!QFileInfo::exists(idfPath)) missingFiles << "idf.utf8";
    if (!QFileInfo::exists(stopWordsPath)) missingFiles << "stop_words.utf8";

    if (!missingFiles.isEmpty()) {
        m_lastError = "缺少词典文件: " + missingFiles.join(", ");
        qWarning() << m_lastError;
        return false;
    }

    try {
        // 创建 Jieba 实例
        m_jieba = std::make_unique<cppjieba::Jieba>(
            dictPath.toStdString(),
            hmmPath.toStdString(),
            userDictPath.toStdString(),
            idfPath.toStdString(),
            stopWordsPath.toStdString()
            );

        m_initialized = true;
        qInfo() << "ChineseTokenizer initialized successfully";
        return true;

    } catch (const std::exception& e) {
        m_lastError = QString("初始化失败: %1").arg(QString::fromStdString(e.what()));
        qWarning() << m_lastError;
        return false;
    }
}

QStringList ChineseTokenizer::tokenize(const QString& text)
{
    if (!m_initialized) {
        qWarning() << "ChineseTokenizer not initialized";
        return QStringList();
    }

    if (text.isEmpty()) {
        return QStringList();
    }

    try {
        std::vector<std::string> words;
        std::string textStd = text.toStdString();

        // 使用精确模式分词
        m_jieba->Cut(textStd, words, false);

        QStringList result;
        for (const auto& word : words) {
            QString qword = QString::fromStdString(word).trimmed();
            // 过滤空白和标点
            if (!qword.isEmpty() && qword != " ") {
                result << qword;
            }
        }

        return result;

    } catch (const std::exception& e) {
        qWarning() << "Tokenization error:" << e.what();
        return QStringList();
    }
}

QStringList ChineseTokenizer::tokenizeEnglish(const QString& text)
{
    if (text.isEmpty()) {
        return QStringList();
    }

    QStringList result;
    QString currentWord;

    for (int i = 0; i < text.length(); ++i) {
        QChar c = text[i];

        // 字母或数字 - 累积到当前单词
        if (c.isLetterOrNumber() || c == '\'' || c == '-') {
            currentWord += c;
        }
        // 空格或标点 - 结束当前单词
        else {
            if (!currentWord.isEmpty()) {
                result << currentWord;
                currentWord.clear();
            }
        }
    }

    // 处理最后一个单词
    if (!currentWord.isEmpty()) {
        result << currentWord;
    }

    return result;
}

QVector<TokenWithPosition> ChineseTokenizer::tokenizeEnglishLine(
    const QString& text,
    const QRect& lineRect,
    int lineIndex)
{
    QVector<TokenWithPosition> result;

    if (text.isEmpty()) {
        return result;
    }

    QString currentWord;
    int wordStartIndex = 0;
    int totalLength = text.length();

    for (int i = 0; i < text.length(); ++i) {
        QChar c = text[i];

        if (c.isLetterOrNumber() || c == '\'' || c == '-') {
            if (currentWord.isEmpty()) {
                wordStartIndex = i;
            }
            currentWord += c;
        } else {
            if (!currentWord.isEmpty()) {
                TokenWithPosition token;
                token.word = currentWord;
                token.startIndex = wordStartIndex;
                token.endIndex = i;
                token.lineIndex = lineIndex;
                token.estimatedRect = estimateWordRectInLine(
                    wordStartIndex, i, totalLength, lineRect
                    );
                result.append(token);
                currentWord.clear();
            }
        }
    }

    // 处理最后一个单词
    if (!currentWord.isEmpty()) {
        TokenWithPosition token;
        token.word = currentWord;
        token.startIndex = wordStartIndex;
        token.endIndex = text.length();
        token.lineIndex = lineIndex;
        token.estimatedRect = estimateWordRectInLine(
            wordStartIndex, text.length(), totalLength, lineRect
            );
        result.append(token);
    }

    return result;
}

QVector<TokenWithPosition> ChineseTokenizer::tokenizeWithPosition(
    const OCRResult& ocr)
{
    QVector<TokenWithPosition> result;
    if (!ocr.success) return result;
    if (ocr.texts.empty() || ocr.boxes.empty()) return result;

    size_t n = std::min(ocr.texts.size(), ocr.boxes.size());

    for (size_t i = 0; i < n; ++i) {
        const std::string& lineStd = ocr.texts[i];
        if (lineStd.empty()) continue;

        QString lineText = QString::fromStdString(lineStd);

        // 1. 这一行对应的 box → 行矩形
        const auto& box = ocr.boxes[i];
        if (box.size() < 4) continue;

        QRect lineRect = boundingRectFromBox(box);

        // 2. 判断是中文还是英文 - 使用工具函数
        bool hasChinese = RapidOCR::Utils::hasChineseChar(lineStd);

        if (!hasChinese) {
            // 英文：按单词分割
            QVector<TokenWithPosition> englishTokens = tokenizeEnglishLine(
                lineText, lineRect, static_cast<int>(i)
                );
            result.append(englishTokens);
        } else {
            // 中文：用 jieba 分词
            if (!m_initialized) {
                qWarning() << "Jieba not initialized for Chinese text";
                continue;
            }

            std::vector<cppjieba::Word> words;
            m_jieba->Cut(lineStd, words, false);

            int totalLength = static_cast<int>(lineStd.length());

            for (const auto& w : words) {
                QString qword = QString::fromStdString(w.word).trimmed();
                if (qword.isEmpty()) continue;
                if (qword.length() == 1 && !qword[0].isLetterOrNumber()) {
                    continue;
                }

                TokenWithPosition token;
                token.word = qword;
                token.startIndex = static_cast<int>(w.offset);
                token.endIndex   = static_cast<int>(w.offset + w.word.length());
                token.lineIndex  = static_cast<int>(i);

                token.estimatedRect = estimateWordRectInLine(
                    token.startIndex,
                    token.endIndex,
                    totalLength,
                    lineRect
                    );

                result.append(token);
            }
        }
    }

    qDebug() << "Tokenized" << result.size() << "words from OCRResult";
    return result;
}

QRect ChineseTokenizer::boundingRectFromBox(
    const std::vector<cv::Point2f>& box)
{
    float minX = box[0].x, maxX = box[0].x;
    float minY = box[0].y, maxY = box[0].y;

    for (size_t i = 1; i < box.size(); ++i) {
        minX = std::min(minX, box[i].x);
        maxX = std::max(maxX, box[i].x);
        minY = std::min(minY, box[i].y);
        maxY = std::max(maxY, box[i].y);
    }

    return QRect(
        static_cast<int>(std::floor(minX)),
        static_cast<int>(std::floor(minY)),
        static_cast<int>(std::ceil(maxX - minX)),
        static_cast<int>(std::ceil(maxY - minY))
        );
}

QRect ChineseTokenizer::estimateWordRectInLine(
    int startIndex,
    int endIndex,
    int totalLength,
    const QRect& lineRect)
{
    if (totalLength <= 0) {
        return lineRect;
    }

    double startRatio = static_cast<double>(startIndex) / totalLength;
    double endRatio   = static_cast<double>(endIndex)   / totalLength;

    int wordLeft  = lineRect.left() + static_cast<int>(startRatio * lineRect.width());
    int wordRight = lineRect.left() + static_cast<int>(endRatio   * lineRect.width());

    if (wordRight <= wordLeft) {
        wordRight = wordLeft + 1;
    }

    return QRect(
        wordLeft,
        lineRect.top(),
        wordRight - wordLeft,
        lineRect.height()
        );
}


TokenWithPosition ChineseTokenizer::findClosestToken(
    const QVector<TokenWithPosition>& tokens,
    const QPoint& mousePos)
{
    if (tokens.isEmpty()) {
        return TokenWithPosition();
    }

    double minDistance = std::numeric_limits<double>::max();
    TokenWithPosition closestToken;

    for (const auto& token : tokens) {
        double dist = distanceToRect(mousePos, token.estimatedRect);

        qDebug() << "Token:" << token.word
                 << "Rect:" << token.estimatedRect
                 << "Distance:" << dist;

        if (dist < minDistance) {
            minDistance = dist;
            closestToken = token;
        }
    }

    qDebug() << "Closest token:" << closestToken.word
             << "Distance:" << minDistance;

    return closestToken;
}

double ChineseTokenizer::distanceToRect(const QPoint& point, const QRect& rect)
{
    // 如果点在矩形内,距离为0
    if (rect.contains(point)) {
        return 0.0;
    }

    // 找到矩形上最近的点
    int closestX = std::clamp(point.x(), rect.left(), rect.right());
    int closestY = std::clamp(point.y(), rect.top(), rect.bottom());

    // 计算欧氏距离
    int dx = point.x() - closestX;
    int dy = point.y() - closestY;

    return std::sqrt(dx * dx + dy * dy);
}
