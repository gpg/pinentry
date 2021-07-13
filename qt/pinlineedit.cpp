/* pinlineedit.cpp - Modified QLineEdit widget.
 * Copyright (C) 2018 Damien Goutte-Gattat
 * Copyright (C) 2021 g10 Code GmbH
 *
 * Software engineering by Ingo Klöcker <dev@ingo-kloecker.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>.
 * SPDX-License-Identifier: GPL-2.0+
 */

#include "pinlineedit.h"

#include <QKeyEvent>

static const int FormattedPassphraseGroupSize = 4;
static const QChar FormattedPassphraseSeparator = QChar::Nbsp;

class PinLineEdit::Private
{
public:
    QString formatted(QString text) const
    {
        const int dashCount = text.size() / FormattedPassphraseGroupSize;
        text.reserve(text.size() + dashCount);
        for (int i = FormattedPassphraseGroupSize; i < text.size(); i += FormattedPassphraseGroupSize + 1) {
            text.insert(i, FormattedPassphraseSeparator);
        }
        return text;
    }

    QString unformatted(QString text) const
    {
        for (int i = FormattedPassphraseGroupSize; i < text.size(); i += FormattedPassphraseGroupSize) {
            text.remove(i, 1);
        }
        return text;
    }

public:
    bool mFormattedPassphrase = false;
};

PinLineEdit::PinLineEdit(QWidget *parent)
    : QLineEdit(parent)
    , d{new Private}
{
    connect(this, SIGNAL(textEdited(QString)),
            this, SLOT(textEdited()));
}

PinLineEdit::~PinLineEdit() = default;

void PinLineEdit::setFormattedPassphrase(bool on)
{
    if (on == d->mFormattedPassphrase) {
        return;
    }
    d->mFormattedPassphrase = on;
    if (d->mFormattedPassphrase) {
        setText(d->formatted(text()));
    } else {
        setText(d->unformatted(text()));
    }
}

void PinLineEdit::setPin(const QString &pin)
{
    setText(d->mFormattedPassphrase ? d->formatted(pin) : pin);
}

QString PinLineEdit::pin() const
{
    if (d->mFormattedPassphrase) {
        return d->unformatted(text());
    } else {
        return text();
    }
}

void PinLineEdit::keyPressEvent(QKeyEvent *e)
{
    QLineEdit::keyPressEvent(e);

    if ( e->key() == Qt::Key::Key_Backspace )
	emit backspacePressed();
}

void PinLineEdit::textEdited()
{
    if (!d->mFormattedPassphrase) {
        return;
    }
    setText(d->formatted(text().remove(FormattedPassphraseSeparator)));
}

#include "pinlineedit.moc"
