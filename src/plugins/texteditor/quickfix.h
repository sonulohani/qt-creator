/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2010 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: Nokia Corporation (qt-info@nokia.com)
**
** Commercial Usage
**
** Licensees holding valid Qt Commercial licenses may use this file in
** accordance with the Qt Commercial License Agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and Nokia.
**
** GNU Lesser General Public License Usage
**
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 2.1 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL included in the
** packaging of this file.  Please review the following information to
** ensure the GNU Lesser General Public License version 2.1 requirements
** will be met: http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** If you are unsure which license is appropriate for your use, please
** contact the sales department at http://qt.nokia.com/contact.
**
**************************************************************************/

#ifndef TEXTEDITORQUICKFIX_H
#define TEXTEDITORQUICKFIX_H

#include "texteditor_global.h"
#include "icompletioncollector.h"

#include <texteditor/refactoringchanges.h>
#include <utils/changeset.h>

#include <QtCore/QSharedPointer>
#include <QtGui/QTextCursor>

namespace TextEditor {

class BaseTextEditor;
class QuickFixOperation;

class TEXTEDITOR_EXPORT QuickFixState
{
    Q_DISABLE_COPY(QuickFixState)

public:
    QuickFixState() {}
    virtual ~QuickFixState() {}
};

class TEXTEDITOR_EXPORT QuickFixOperation
{
    Q_DISABLE_COPY(QuickFixOperation)

public:
    typedef QSharedPointer<QuickFixOperation> Ptr;

public:
    QuickFixOperation(TextEditor::BaseTextEditor *editor);
    virtual ~QuickFixOperation();

    virtual QString description() const = 0;
    virtual void createChanges() = 0;

    virtual int match(QuickFixState *state) = 0;

    void perform();

    TextEditor::BaseTextEditor *editor() const;

    QTextCursor textCursor() const;
    void setTextCursor(const QTextCursor &cursor);

    int selectionStart() const;
    int selectionEnd() const;

    int position(int line, int column) const;

    QChar charAt(int offset) const;
    QString textOf(int start, int end) const;

    static TextEditor::RefactoringChanges::Range range(int start, int end);

protected:
    virtual void apply() = 0;
    virtual TextEditor::RefactoringChanges *refactoringChanges() const = 0;

private:
    TextEditor::BaseTextEditor *_editor;
    QTextCursor _textCursor;
};


class TEXTEDITOR_EXPORT QuickFixCollector: public TextEditor::IQuickFixCollector
{
    Q_OBJECT

public:
    QuickFixCollector();
    virtual ~QuickFixCollector();

    QList<TextEditor::QuickFixOperation::Ptr> quickFixes() const { return _quickFixes; }

    virtual TextEditor::ITextEditable *editor() const;
    virtual int startPosition() const;
    virtual bool triggersCompletion(TextEditor::ITextEditable *editor);
    virtual int startCompletion(TextEditor::ITextEditable *editor);
    virtual void completions(QList<TextEditor::CompletionItem> *completions);
    virtual void complete(const TextEditor::CompletionItem &item);
    virtual void cleanup();

    virtual TextEditor::QuickFixState *initializeCompletion(TextEditor::ITextEditable *editable) = 0;
    virtual QList<TextEditor::QuickFixOperation::Ptr> quickFixOperations(TextEditor::BaseTextEditor *editor) const;

private:
    TextEditor::ITextEditable *_editable;
    QList<TextEditor::QuickFixOperation::Ptr> _quickFixes;
};

class TEXTEDITOR_EXPORT IQuickFixFactory: public QObject
{
    Q_OBJECT

public:
    IQuickFixFactory(QObject *parent = 0);
    virtual ~IQuickFixFactory();

    virtual QList<QuickFixOperation::Ptr> quickFixOperations(TextEditor::BaseTextEditor *editor) = 0;
};

} // end of namespace TextEditor

#endif // TEXTEDITORQUICKFIX_H
