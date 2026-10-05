#pragma once
// Port of TextRecognizer.swift: on-device OCR.
//
// Windows implementation: the Windows.Media.Ocr WinRT engine, driven through a short Windows PowerShell
// script (the MinGW toolchain ships no WinRT headers, so it is not called in-process). Each recognize()
// call therefore costs a process start (about half a second). When Windows PowerShell or an OCR language
// pack is missing, isAvailable() is false and recognize() returns an empty string, so scanned pages are
// simply skipped, exactly like the Swift behaviour when Vision finds no text.
#include <QImage>
#include <QString>
#include <QStringList>

namespace wp::TextRecognizer {

/// Languages we ask for when the engine supports them, in priority order.
const QStringList &preferredLanguages();

/// The preferred languages (BCP-47 tags) this machine has an OCR engine for. Empty when OCR is unavailable.
/// Computed once.
const QStringList &languages();

/// True when at least one OCR engine can be created (a user-profile language engine counts too).
bool isAvailable();

/// Returns the recognized lines top to bottom, or an empty string.
QString recognize(const QImage &image);

} // namespace wp::TextRecognizer
