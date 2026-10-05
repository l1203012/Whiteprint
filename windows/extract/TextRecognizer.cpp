#include "extract/TextRecognizer.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QProcess>
#include <QTemporaryDir>

namespace wp::TextRecognizer {

namespace {

// Windows PowerShell 5.1 can project WinRT types; PowerShell 7 cannot, so the inbox powershell.exe is used.
// Output: `-List` prints one supported language tag per line and `profile` when an engine for the user's
// profile languages exists; otherwise the recognized lines.
const char *const script = R"PS(
param([string]$Path, [string]$Langs, [switch]$List)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Runtime.WindowsRuntime
$null = [Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime]
$null = [Windows.Storage.StorageFile, Windows.Foundation, ContentType = WindowsRuntime]
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
if ($List) {
  foreach ($l in [Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages) { [Console]::Out.WriteLine($l.LanguageTag) }
  if ([Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages()) { [Console]::Out.WriteLine('profile') }
  exit 0
}
$asTask = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' })[0]
function Await($op, $type) { $t = $asTask.MakeGenericMethod($type).Invoke($null, @($op)); $t.Wait(-1) | Out-Null; $t.Result }
$engine = $null
foreach ($l in ($Langs -split ',')) {
  if ($l -and -not $engine) {
    try {
      $lang = New-Object Windows.Globalization.Language($l)
      if ([Windows.Media.Ocr.OcrEngine]::IsLanguageSupported($lang)) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang) }
    } catch {}
  }
}
if (-not $engine) { $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromUserProfileLanguages() }
if (-not $engine) { exit 3 }
$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($Path)) ([Windows.Storage.StorageFile])
$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bmp = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$result = Await ($engine.RecognizeAsync($bmp)) ([Windows.Media.Ocr.OcrResult])
foreach ($line in $result.Lines) { [Console]::Out.WriteLine($line.Text) }
)PS";

QString powershellPath() {
    const QString root = qEnvironmentVariable("SystemRoot", QStringLiteral("C:\\Windows"));
    const QString inbox = root + QStringLiteral("\\System32\\WindowsPowerShell\\v1.0\\powershell.exe");
    return QFile::exists(inbox) ? inbox : QStringLiteral("powershell.exe");
}

/// Runs the script in `dir`; returns stdout lines, or nullopt when the process failed.
bool runScript(const QStringList &arguments, QString &output) {
#ifdef Q_OS_WIN
    QTemporaryDir dir;
    if (!dir.isValid()) return false;
    const QString scriptPath = dir.filePath(QStringLiteral("ocr.ps1"));
    QFile file(scriptPath);
    if (!file.open(QIODevice::WriteOnly)) return false;
    // A UTF-8 BOM makes Windows PowerShell 5.1 read the script as UTF-8.
    file.write("\xEF\xBB\xBF");
    file.write(script);
    file.close();

    QProcess process;
    process.setProgram(powershellPath());
    process.setArguments(QStringList{QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"), QStringLiteral("-ExecutionPolicy"),
                                     QStringLiteral("Bypass"), QStringLiteral("-File"), scriptPath}
                         + arguments);
    process.start();
    if (!process.waitForStarted(10'000)) return false;
    if (!process.waitForFinished(90'000)) {
        process.kill();
        process.waitForFinished(2'000);
        return false;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) return false;
    output = QString::fromUtf8(process.readAllStandardOutput());
    return true;
#else
    Q_UNUSED(arguments);
    Q_UNUSED(output);
    return false;
#endif
}

struct Capabilities {
    QStringList languages;
    bool available = false;
};

const Capabilities &capabilities() {
    static const Capabilities caps = [] {
        Capabilities c;
        QString out;
        if (!runScript({QStringLiteral("-List")}, out)) return c;
        QStringList supported;
        for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            const QString tag = line.trimmed();
            if (tag == QLatin1String("profile")) c.available = true;
            else if (!tag.isEmpty()) supported << tag.toLower();
        }
        for (const QString &preferred : preferredLanguages())
            if (supported.contains(preferred.toLower())) c.languages << preferred;
        if (!supported.isEmpty()) c.available = true;
        return c;
    }();
    return caps;
}

} // namespace

const QStringList &preferredLanguages() {
    static const QStringList list{QStringLiteral("en-US"), QStringLiteral("nl-NL"), QStringLiteral("fr-FR"), QStringLiteral("de-DE"),
                                  QStringLiteral("es-ES"), QStringLiteral("it-IT"), QStringLiteral("pt-BR")};
    return list;
}

const QStringList &languages() {
    return capabilities().languages;
}

bool isAvailable() {
    return capabilities().available;
}

QString recognize(const QImage &image) {
    if (image.isNull() || !isAvailable()) return {};
    QTemporaryDir dir;
    if (!dir.isValid()) return {};
    const QString imagePath = dir.filePath(QStringLiteral("page.png"));
    if (!image.save(imagePath, "PNG")) return {};
    QString output;
    // Windows OCR works with one language at a time: the first supported one in priority order.
    const QStringList langs = languages();
    if (!runScript({QStringLiteral("-Path"), QDir::toNativeSeparators(imagePath), QStringLiteral("-Langs"), langs.join(QLatin1Char(','))}, output))
        return {};
    QStringList lines;
    for (const QString &line : output.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        QString trimmed = line;
        if (trimmed.endsWith(QLatin1Char('\r'))) trimmed.chop(1);
        if (!trimmed.isEmpty()) lines << trimmed;
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace wp::TextRecognizer
