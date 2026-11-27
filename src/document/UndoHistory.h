#pragma once
#include <deque>
#include <cstddef>
#include <utility>

namespace thecad::document {
// History belongs to an editing session, never to the saved parametric feature tree.
// Callers validate/display a target before committing a history traversal.
template<class State> class UndoHistory {
public:
    explicit UndoHistory(std::size_t limit=10):m_limit(limit){}
    bool canUndo() const {return !m_undo.empty();}
    bool canRedo() const {return !m_redo.empty();}
    const State& undoTarget() const {return m_undo.back();}
    const State& redoTarget() const {return m_redo.back();}
    void record(State previous){m_redo.clear();append(m_undo,std::move(previous));}
    void commitUndo(State current){append(m_redo,std::move(current));m_undo.pop_back();}
    void commitRedo(State current){append(m_undo,std::move(current));m_redo.pop_back();}
    void clear(){m_undo.clear();m_redo.clear();}
private:
    void append(std::deque<State>& states,State value){
        if(!m_limit)return;
        states.push_back(std::move(value));if(states.size()>m_limit)states.pop_front();
    }
    std::size_t m_limit;
    std::deque<State> m_undo,m_redo;
};
}
