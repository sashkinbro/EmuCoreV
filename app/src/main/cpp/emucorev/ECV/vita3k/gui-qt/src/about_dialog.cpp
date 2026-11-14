// Vita3K emulator project
// Copyright (C) 2026 Vita3K team
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

#include "ui_about_dialog.h"

#include <gui-qt/about_dialog.h>

#include <config/version.h>
#include <emuenv/state.h>

#include <QDesktopServices>
#include <QDialog>
#include <QStringLiteral>
#include <QUrl>
#include <QWidget>

AboutDialog::AboutDialog(EmuEnvState &emuenv, QWidget *parent)
    : QDialog(parent)
    , emuenv(emuenv)
    , m_ui(std::make_unique<Ui::AboutDialog>()) {
    m_ui->setupUi(this);

    m_ui->close_button->setDefault(true);
    m_ui->icon_label->setPixmap(
        QPixmap(":/Vita3K.png").scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation));

    m_ui->description_label->setTextFormat(Qt::RichText);
    m_ui->description_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_ui->description_label->setOpenExternalLinks(true);

    m_ui->credit_label->setTextFormat(Qt::RichText);
    m_ui->credit_label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    m_ui->credit_label->setOpenExternalLinks(true);

    const QString version = QString::fromUtf8(core_version) + " (based on "
        + QString::fromUtf8(upstream_version) + ")-"
        + QString::number(app_number) + "-" + QString::fromUtf8(app_hash);
    m_ui->version_label->setText(tr("Version: %1").arg(version));

    m_ui->description_label->setText(tr(
        "EmuCoreV (ECV) is an open-source PS Vita\u2122/PS TV\u2122 emulator based on Vita3K.\n"
        "Visit our website at <a href=\"https://emucorev.web.app\">emucorev.web.app</a> for more info. "
        "If you're interested in contributing, check out our <a href=\"https://github.com/sashkinbro/EmuCoreV\">GitHub</a>. "
        "Support EmuCore via <a href=\"https://www.patreon.com/c/emucore/membership\">Patreon</a>. "
        "Upstream source: <a href=\"https://github.com/Vita3K/Vita3K\">Vita3K</a>."));

    m_ui->credit_label->setText(
        tr("Icon by %1").arg(QStringLiteral("<a href=\"https://gordonmackayillustration.blogspot.com\">Gordon Mackay</a>")));

    connect(m_ui->github_button, &QPushButton::clicked, [] {
        QDesktopServices::openUrl(QUrl("https://github.com/sashkinbro/EmuCoreV"));
    });
    connect(m_ui->website_button, &QPushButton::clicked, [] {
        QDesktopServices::openUrl(QUrl("https://emucorev.web.app"));
    });
    connect(m_ui->kofi_button, &QPushButton::clicked, [] {
        QDesktopServices::openUrl(QUrl("https://www.patreon.com/c/emucore/membership"));
    });
    connect(m_ui->discord_button, &QPushButton::clicked, [] {
        QDesktopServices::openUrl(QUrl("https://discord.gg/c5EBeNRpz2"));
    });
    connect(m_ui->close_button, &QPushButton::clicked, this, &QWidget::close);
}

AboutDialog::~AboutDialog() = default;
