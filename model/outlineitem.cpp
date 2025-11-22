#include "outlineitem.h"

OutlineItem::OutlineItem(const QString& title, int pageIndex, const QString& uri)
    : m_title(title)
    , m_pageIndex(pageIndex)
    , m_uri(uri)
    , m_parent(nullptr)
{
}

OutlineItem::~OutlineItem()
{
    // 释放所有子节点
    clearChildren();
}

void OutlineItem::addChild(OutlineItem* child)
{
    if (child) {
        child->setParent(this);
        m_children.append(child);
    }
}

bool OutlineItem::insertChild(int index, OutlineItem* child)
{
    if (!child) {
        return false;
    }

    if (index < 0 || index > m_children.size()) {
        return false;
    }

    child->setParent(this);
    m_children.insert(index, child);
    return true;
}

bool OutlineItem::removeChild(OutlineItem* child)
{
    if (!child) {
        return false;
    }

    int index = m_children.indexOf(child);
    if (index < 0) {
        return false;
    }

    m_children.removeAt(index);
    child->m_parent = nullptr;

    return true;
}

OutlineItem* OutlineItem::takeChild(int index)
{
    if (index < 0 || index >= m_children.size()) {
        return nullptr;
    }

    OutlineItem* child = m_children.takeAt(index);
    if (child) {
        child->setParent(nullptr);
    }

    return child;
}

void OutlineItem::clearChildren()
{
    qDeleteAll(m_children);
    m_children.clear();
}

OutlineItem* OutlineItem::child(int index) const
{
    if (index >= 0 && index < m_children.size()) {
        return m_children[index];
    }
    return nullptr;
}

int OutlineItem::indexOf(OutlineItem* child) const
{
    return m_children.indexOf(child);
}

int OutlineItem::depth() const
{
    int d = 0;
    OutlineItem* p = m_parent;
    while (p) {
        ++d;
        p = p->parent();
    }
    return d;
}
