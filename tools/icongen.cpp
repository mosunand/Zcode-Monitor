// icongen.cpp — generates the zcode-monitor logo as a multi-resolution
// Windows .ico + a 256px PNG for the README. Drawn programmatically with
// QPainter so the logo stays in sync with the app's dark theme tokens.
//
// usage: icongen <output-dir>

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QVector>

#include <cstdio>

namespace {

// master render at `size` px; all coordinates scale from the 256-design grid
QImage drawIcon(int size)
{
    const double s = double(size) / 256.0;
    QImage img(size, size, QImage::Format_RGBA8888_Premultiplied);
    img.fill(Qt::transparent);

    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal r = [size]() { return size >= 64 ? 6.0 : 4.0; }(); // radius per 16px step
    const qreal radius = 52 * s + r;

    // ── dark rounded badge (theme surface gradient) ──
    QLinearGradient bg(QPointF(0, 0), QPointF(size, size));
    bg.setColorAt(0, QColor(0x14, 0x1a, 0x24));
    bg.setColorAt(1, QColor(0x0b, 0x0d, 0x12));
    p.setPen(QPen(QColor(0x38, 0xbd, 0xf8, 110), qMax(1.0, 3 * s))); // thin cyan ring
    p.setBrush(bg);
    p.drawRoundedRect(QRectF(6 * s, 6 * s, size - 12 * s, size - 12 * s), radius, radius);

    p.setPen(Qt::NoPen);

    // ── pulse line (monitoring heartbeat), cyan with glow ──
    QPainterPath pulse;
    pulse.moveTo(38 * s, 138 * s);
    pulse.lineTo(78 * s, 138 * s);
    pulse.lineTo(96 * s, 138 * s);
    pulse.lineTo(112 * s, 168 * s); // small dip
    pulse.lineTo(134 * s, 70 * s);  // main spike
    pulse.lineTo(154 * s, 178 * s); // deep valley
    pulse.lineTo(174 * s, 138 * s);
    pulse.lineTo(218 * s, 138 * s);

    QColor glow = QColor(0x38, 0xbd, 0xf8, 55);
    p.setPen(QPen(glow, qMax(2.0, 30 * s), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    p.drawPath(pulse);
    p.setPen(QPen(QColor(0x38, 0xbd, 0xf8), qMax(1.5, 16 * s), Qt::SolidLine,
                  Qt::RoundCap, Qt::RoundJoin));
    p.drawPath(pulse);

    // ── live status dot, top-right (theme sevOk) ──
    QColor dotGlow = QColor(0x4a, 0xde, 0x80, 80);
    p.setBrush(dotGlow);
    p.drawEllipse(QPointF(196 * s, 80 * s), 34 * s, 34 * s);
    p.setBrush(QColor(0x4a, 0xde, 0x80));
    p.drawEllipse(QPointF(196 * s, 80 * s), 21 * s, 21 * s);

    // ── dashboard bars, bottom (theme category colors) ──
    struct Bar {
        qreal x, h;
        QColor c;
    };
    const Bar bars[] = {
        {64.0, 34.0, QColor(0x14, 0xb8, 0xa6)}, // teal
        {118.0, 58.0, QColor(0xa7, 0x8b, 0xfa)}, // purple
        {172.0, 22.0, QColor(0x60, 0xa5, 0xfa)}, // blue
    };
    for (const Bar& b : bars) {
        QColor c = b.c;
        c.setAlpha(200);
        p.setBrush(c);
        const qreal bw = 26 * s;
        const qreal bx = b.x * s;
        const qreal by = 206 * s - b.h * s;
        p.drawRoundedRect(QRectF(bx, by, bw, b.h * s + 6 * s), 4 * s, 4 * s);
    }

    p.end();
    return img;
}

bool writeIco(const QString& path, const QVector<QPair<QByteArray, int>>& entries)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    QDataStream ds(&f);
    ds.setByteOrder(QDataStream::LittleEndian);
    ds << quint16(0) << quint16(1) << quint16(entries.size());
    quint32 offset = 6 + quint32(entries.size()) * 16;
    for (const auto& entry : entries) {
        const int size = entry.second;
        ds << quint8(size >= 256 ? 0 : size)  // width (0 = 256)
           << quint8(size >= 256 ? 0 : size)  // height
           << quint8(0)                       // palette
           << quint8(0)                       // reserved
           << quint16(1)                      // planes
           << quint16(32)                     // bpp
           << quint32(entry.first.size())
           << quint32(offset);
        offset += quint32(entry.first.size());
    }
    for (const auto& entry : entries)
        f.write(entry.first);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    QGuiApplication app(argc, argv);
    const QString outDir = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral(".");

    const int sizes[] = {16, 24, 32, 48, 64, 128, 256};
    const QImage master = drawIcon(1024);

    QVector<QPair<QByteArray, int>> entries;
    for (int size : sizes) {
        QImage frame = master.scaled(size, size, Qt::IgnoreAspectRatio,
                                     Qt::SmoothTransformation);
        QByteArray png;
        QBuffer buf(&png);
        buf.open(QIODevice::WriteOnly);
        frame.save(&buf, "PNG");
        entries.append({png, size});
        if (size == 256)
            frame.save(outDir + QStringLiteral("/logo-256.png"));
    }

    if (!writeIco(outDir + QStringLiteral("/zcode-monitor.ico"), entries)) {
        fprintf(stderr, "ico write failed\n");
        return 1;
    }
    printf("icon written: %d sizes\n", int(entries.size()));
    return 0;
}
