#include "Document/EditorSession.h"
#include <cmath>

// Select's Expand, Contract and Feather, and their amounts.
void EditorSession::setSelectionExpandAmount(int amount)
{
    m_selectionExpandAmount = amount;
    notify();
}

void EditorSession::setSelectionContractAmount(int amount)
{
    m_selectionContractAmount = amount;
    notify();
}

void EditorSession::setSelectionFeatherAmount(int amount)
{
    m_selectionFeatherAmount = amount;
    notify();
}

void EditorSession::setSelectionAmountOperation(std::optional<SelectionAmountOperation> operation)
{
    m_selectionAmountOperation = operation;
    resumeFileRequests();
    notify();
}

bool EditorSession::canModifySelection() const
{
    const std::optional<DocumentSelection> current = selection();
    return current && !current->isEmpty() && canEditSelection() && !m_lassoDraft;
}

void EditorSession::expandSelection(int amount)
{
    resizeSelection(amount, QStringLiteral("Expand Selection"));
}

void EditorSession::contractSelection(int amount)
{
    resizeSelection(-amount, QStringLiteral("Contract Selection"));
}

void EditorSession::promptSelectionAmount(SelectionAmountOperation operation)
{
    if (!canModifySelection())
        return;
    setSelectionAmountOperation(operation);
}

void EditorSession::confirmSelectionAmount(int amount)
{
    const std::optional<SelectionAmountOperation> operation = m_selectionAmountOperation;
    if (!operation || amount < 1 || amount > (*operation == SelectionAmountOperation::feather ? 250 : 500))
        return;
    setSelectionAmountOperation(std::nullopt);
    switch (*operation) {
    case SelectionAmountOperation::expand:
        setSelectionExpandAmount(amount);
        expandSelection(amount);
        break;
    case SelectionAmountOperation::contract:
        setSelectionContractAmount(amount);
        contractSelection(amount);
        break;
    case SelectionAmountOperation::feather:
        setSelectionFeatherAmount(amount);
        featherSelection(amount);
        break;
    }
}

void EditorSession::featherSelection(int amount)
{
    const std::optional<DocumentSelection> current = selection();
    if (!canModifySelection() || !current || amount <= 0)
        return;
    // Two soft edges spread less than their sum.
    const double softened = std::sqrt(current->feather * current->feather + double(amount) * amount);
    setSelection(DocumentSelection{current->path, current->antialiased, std::min(250.0, softened)}, QStringLiteral("Feather Selection"));
}
