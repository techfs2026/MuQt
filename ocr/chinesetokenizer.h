#ifndef CHINESETOKENIZER_H
#define CHINESETOKENIZER_H

#include <QString>
#include <QStringList>
#include <QPoint>
#include <QRect>
#include <memory>
#include "cppjieba/Jieba.hpp"
#include "ocrengine.h"

/**
 * @brief 分词结果 - 带位置信息的词
 */
struct TokenWithPosition {
    QString word;           // 词语
    int startIndex;         // 在原文本中的起始位置
    int endIndex;           // 在原文本中的结束位置
    QRect estimatedRect;    // 估算的位置矩形(基于字符位置)
    int lineIndex  = -1;

    bool isValid() const { return !word.isEmpty(); }
};

/**
 * @brief 中文分词器
 *
 * 使用 cppjieba 进行中文分词,并计算每个词的大致位置
 * 支持中英文混合文本的智能分词
 */
class ChineseTokenizer
{
public:
    static ChineseTokenizer& instance();

    /**
     * @brief 初始化分词器
     * @param dictDir 词典目录路径
     */
    bool initialize(const QString& dictDir);

    /**
     * @brief 检查是否已初始化
     */
    bool isInitialized() const { return m_initialized; }

    /**
     * @brief 对文本进行分词
     * @param text 待分词文本
     * @return 分词结果列表
     */
    QStringList tokenize(const QString& text);

    /**
     * @brief 对英文文本按单词分割
     * @param text 英文文本
     * @return 单词列表
     */
    QStringList tokenizeEnglish(const QString& text);

    /**
     * @brief 对OCR结果进行分词（带位置信息）
     * 自动识别中英文，使用对应的分词策略
     * @param ocr OCR识别结果
     * @return 分词结果列表（带位置）
     */
    QVector<TokenWithPosition> tokenizeWithPosition(const OCRResult& ocr);

    /**
     * @brief 从四边形框计算边界矩形
     */
    QRect boundingRectFromBox(const std::vector<cv::Point2f>& box);

    /**
     * @brief 估算词语在行内的矩形位置
     */
    QRect estimateWordRectInLine(
        int startIndex,
        int endIndex,
        int totalLength,
        const QRect& lineRect);

    /**
     * @brief 从分词结果中找到最接近指定位置的词
     * @param tokens 分词结果
     * @param mousePos 鼠标位置(全局坐标)
     * @return 最接近的词,如果没有找到返回空
     */
    TokenWithPosition findClosestToken(
        const QVector<TokenWithPosition>& tokens,
        const QPoint& mousePos
        );

    /**
     * @brief 获取最后的错误信息
     */
    QString lastError() const { return m_lastError; }

private:
    ChineseTokenizer();
    ~ChineseTokenizer();

    ChineseTokenizer(const ChineseTokenizer&) = delete;
    ChineseTokenizer& operator=(const ChineseTokenizer&) = delete;

    /**
     * @brief 计算点到矩形的距离
     */
    double distanceToRect(const QPoint& point, const QRect& rect);

    /**
     * @brief 对英文行进行单词分割（带位置）
     */
    QVector<TokenWithPosition> tokenizeEnglishLine(
        const QString& text,
        const QRect& lineRect,
        int lineIndex
        );

private:
    std::unique_ptr<cppjieba::Jieba> m_jieba;
    bool m_initialized;
    QString m_lastError;
    QString m_dictDir;
};

#endif // CHINESETOKENIZER_H
