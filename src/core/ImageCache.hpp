#pragma once

#include "../raw/RawDecoder.hpp"
#include <QHash>
#include <QList>
#include <QString>

namespace mylr {

// Small LRU cache of decoded RawImage previews, bounded by both a count and a
// total-bytes budget. Lets the app flip between nearby photos without paying the
// LibRaw decode cost again. Main-thread use only.
class ImageCache {
public:
    explicit ImageCache(int maxCount = 20, qint64 maxBytes = 4LL * 1024 * 1024 * 1024)
        : m_maxCount(maxCount), m_maxBytes(maxBytes) {}

    bool get(const QString& key, RawImage& out) {
        auto it = m_map.find(key);
        if (it == m_map.end()) return false;
        m_order.removeOne(key);
        m_order.prepend(key);
        out = it.value();
        return true;
    }

    void put(const QString& key, const RawImage& img) {
        if (m_map.contains(key)) {
            m_bytes -= sizeOf(m_map.value(key));
            m_order.removeOne(key);
        }
        m_map.insert(key, img);
        m_order.prepend(key);
        m_bytes += sizeOf(img);
        evict();
    }

    void clear() {
        m_map.clear();
        m_order.clear();
        m_bytes = 0;
    }

private:
    static qint64 sizeOf(const RawImage& img) {
        return static_cast<qint64>(img.preview.sizeInBytes()) +
               static_cast<qint64>(img.linearRgb.sizeInBytes());
    }

    void evict() {
        while (!m_order.isEmpty() &&
               (m_order.size() > m_maxCount || m_bytes > m_maxBytes)) {
            const QString victim = m_order.takeLast();
            m_bytes -= sizeOf(m_map.value(victim));
            m_map.remove(victim);
        }
    }

    int m_maxCount;
    qint64 m_maxBytes;
    qint64 m_bytes = 0;
    QList<QString> m_order;  // front = most recently used
    QHash<QString, RawImage> m_map;
};

} // namespace mylr
