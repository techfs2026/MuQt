#include "ocroutput.h"
#include <numeric>
#include <sstream>
#include <QJsonDocument>

namespace RapidOCR {

double RapidOCROutput::getElapse() const {  // 返回值改为 double
    return std::accumulate(elapseList.begin(), elapseList.end(), 0.0);  // 初始值用 0.0
}

size_t RapidOCROutput::size() const {
    if (!txts.has_value()) {
        return 0;
    }
    return txts->size();
}

bool RapidOCROutput::hasValidData() const {
    return boxes.has_value() && txts.has_value() && scores.has_value();
}

std::optional<QJsonArray> RapidOCROutput::toJson() const {
    if (!hasValidData()) {
        // LOG: The identified content is empty
        return std::nullopt;
    }
    return ToJSON::to(*boxes, *txts, *scores);
}

std::string RapidOCROutput::toMarkdown() const {
    return ToMarkdown::to(boxes, txts);
}

// ToJSON实现
std::optional<QJsonArray> ToJSON::to(
    const std::vector<std::vector<cv::Point2f>>& boxes,
    const std::vector<std::string>& txts,
    const std::vector<float>& scores
    ) {
    if (boxes.size() != txts.size() || boxes.size() != scores.size()) {
        return std::nullopt;
    }

    QJsonArray jsonArray;

    for (size_t i = 0; i < boxes.size(); ++i) {
        QJsonObject item;

        // 添加文本
        item["text"] = QString::fromStdString(txts[i]);

        // 添加置信度
        item["score"] = scores[i];

        // 添加边界框
        QJsonArray boxArray;
        for (const auto& point : boxes[i]) {
            QJsonArray pointArray;
            pointArray.append(point.x);
            pointArray.append(point.y);
            boxArray.append(pointArray);
        }
        item["box"] = boxArray;

        jsonArray.append(item);
    }

    return jsonArray;
}

// ToMarkdown实现
std::string ToMarkdown::to(
    const std::optional<std::vector<std::vector<cv::Point2f>>>& boxes,
    const std::optional<std::vector<std::string>>& txts
    ) {
    std::stringstream ss;

    if (!txts.has_value() || txts->empty()) {
        ss << "| Text |\n";
        ss << "|------|\n";
        ss << "| (No text detected) |\n";
        return ss.str();
    }

    // 创建Markdown表格
    if (boxes.has_value() && boxes->size() == txts->size()) {
        ss << "| Text | Box |\n";
        ss << "|------|-----|\n";

        for (size_t i = 0; i < txts->size(); ++i) {
            ss << "| " << (*txts)[i] << " | ";

            // 输出边界框坐标
            const auto& box = (*boxes)[i];
            ss << "[";
            for (size_t j = 0; j < box.size(); ++j) {
                if (j > 0) ss << ", ";
                ss << "(" << box[j].x << ", " << box[j].y << ")";
            }
            ss << "] |\n";
        }
    } else {
        // 只有文本，没有边界框
        ss << "| Text |\n";
        ss << "|------|\n";

        for (const auto& txt : *txts) {
            ss << "| " << txt << " |\n";
        }
    }

    return ss.str();
}

} // namespace RapidOCR
