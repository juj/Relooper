
#include "Relooper.h"

// TODO: move all set to unorderedset

void PrintIndented(const char *Format, ...) {
  for (int i = 0; i < Indenter::CurrIndent*2; i++) putc(' ', stdout);
  va_list Args;
  va_start(Args, Format);
  vprintf(Format, Args);
  va_end(Args);
}

// Indenter

int Indenter::CurrIndent = 0;

// Branch

void Branch::Render(Block *Target) {
  if (Set) PrintIndented("label = %d;\n", Target->Id);
  if (Ancestor) {
    PrintIndented("%s L%d;\n", Break ? "break" : "continue", Ancestor->Id);
  }
}

// Block

int Block::IdCounter = 0;

// Shape

int Shape::IdCounter = 0;

// MultipleShape

void MultipleShape::Render() {
  bool First = true;
  for (BlockShapeMap::iterator iter = InnerMap.begin(); iter != InnerMap.end(); iter++) {
    PrintIndented("%s if (label == %d) {\n", First ? "" : "else ", iter->first);
    First = false;
    Indenter::Indent();
    iter->second->Render();
    Indenter::Unindent();
    PrintIndented("}\n");
  }
  if (Next) Next->Render();
};

// LoopShape

void LoopShape::Render() {
  PrintIndented("while(1) {\n");
  Indenter::Indent();
  Inner->Render();
  Indenter::Unindent();
  PrintIndented("}\n");
  if (Next) Next->Render();
};

// EmulatedShape

void EmulatedShape::Render() {
  PrintIndented("while(1) {\n");
  Indenter::Indent();
  PrintIndented("switch(label) {\n");
  Indenter::Indent();
  for (int i = 0; i < Blocks.size(); i++) {
    Block *Curr = Blocks[i];
    PrintIndented("case %d: {\n", Curr->Id);
    Indenter::Indent();
    Curr->Render();
    PrintIndented("break;\n");
    Indenter::Unindent();
    PrintIndented("}\n");
  }
  Indenter::Unindent();
  PrintIndented("}\n");
  Indenter::Unindent();
  PrintIndented("}\n");
  if (Next) Next->Render();
};

// Relooper

Relooper::~Relooper() {
  // Delete shapes..
}

void Relooper::Calculate(Block *Entry) {
  Shapes.reserve(Blocks.size()/2); // vague heuristic, better than nothing

  // Add incoming branches
  for (int i = 0; i < Blocks.size(); i++) {
    Block *Curr = Blocks[i];
    for (BlockBranchMap::iterator iter = Curr->BranchesOut.begin(); iter != Curr->BranchesOut.end(); iter++) {
      iter->first->BranchesIn[Curr] = new Branch(); // XXX leaky
    }
  }

  // Recursively process the graph

  struct Recursor {
    Relooper *Parent;
    Recursor(Relooper *ParentInit) : Parent(ParentInit) {}

    // Add a shape to the list of shapes in this Relooper calculation
    void Notice(Shape *New) {
      Parent->Shapes.push_back(New);
    }

    // Create a list of entries from a block
    void GetBlocksOut(Block *Source, BlockSet& Entries) {
      for (BlockBranchMap::iterator iter = Source->BranchesOut.begin(); iter != Source->BranchesOut.end(); iter++) {
        Entries.insert(iter->first);
      }
    }

    // Converts/processes all branchings to a specific target
    void Solipsize(Block *Target, Branch::FlowType Type, Shape *Ancestor) {
      for (BlockBranchMap::iterator iter = Target->BranchesIn.begin(); iter != Target->BranchesIn.end();) {
        Block *Prior = iter->first;
        Branch *TargetIn = iter->second;
        Branch *PriorOut = Prior->BranchesOut[Target];
        PriorOut->Ancestor = Ancestor; // Do we need this info
        PriorOut->Type = Type;         // on TargetIn too?
        iter++; // carefully increment iter before erasing
        Target->BranchesIn.erase(Prior);
        Target->ProcessedBranchesIn[Prior] = TargetIn;
        Prior->BranchesIn.erase(Target);
        Prior->ProcessedBranchesIn[Target] = PriorOut;
      }
    }

    Shape *MakeLoop(BlockSet &Blocks, BlockSet& Entries) {
      // Find the inner blocks in this loop. Proceed backwards from the entries until
      // you reach a seen block, collecting as you go.
      BlockSet InnerBlocks;
      BlockSet Queue = Entries;
      while (Queue.size() > 0) {
        Block *Curr = *(Queue.begin());
        Queue.erase(Queue.begin());
        if (InnerBlocks.find(Curr) == InnerBlocks.end()) {
          // This element is new, mark it as inner and remove from outer
          InnerBlocks.insert(Curr);
          Blocks.erase(Curr);
          // Add the elements prior to it
          for (BlockBranchMap::iterator iter = Curr->BranchesIn.begin(); iter != Curr->BranchesIn.end(); iter++) {
            Queue.insert(iter->first);
          }
        }
      }
      BlockSet NextEntries;
      for (BlockSet::iterator iter = InnerBlocks.begin(); iter != InnerBlocks.end(); iter++) {
        Block *Curr = *iter;
        for (BlockBranchMap::iterator iter = Curr->BranchesOut.begin(); iter != Curr->BranchesOut.end(); iter++) {
          Block *Possible = iter->first;
          if (InnerBlocks.find(Possible) == InnerBlocks.end() &&
              NextEntries.find(Possible) == NextEntries.find(Possible)) {
            NextEntries.insert(Possible);
          }
        }
      }

      // TODO: Optionally hoist additional blocks into the loop

      LoopShape *Loop = new LoopShape();
      Notice(Loop);

      // Solipsize the loop, replacing with break/continue and marking branches as Processed (will not affect later calculations)
      // A. Branches to the loop entries become a continue to this shape
      for (BlockSet::iterator iter = Entries.begin(); iter != Entries.end(); iter++) {
        Solipsize(*iter, Branch::Continue, Loop);
      }
      // B. Branches to outside the loop (a next entry) become breaks on this shape
      for (BlockSet::iterator iter = NextEntries.begin(); iter != NextEntries.end(); iter++) {
        Solipsize(*iter, Branch::Break, Loop);
      }
      // Finish up
      Shape *Inner = Process(InnerBlocks, Entries);
      Loop->Inner = Inner;
      Loop->Next = NextEntries.size() > 0 ? Process(Blocks, NextEntries) : NULL;
      return Loop;
    }

    //void FindIndependentGroups(BlockSet &Blocks, BlockVec &Entries, BlockBlockVec& IndependentGroups) {
    //}

    Shape *MakeMultiple(BlockSet &Blocks, BlockVec &Entries, BlockBlockVec& IndependentGroups) {
      return NULL;
    }

    // Main function.
    // Process a set of blocks with specified entries, returns a shape
    Shape *Process(BlockSet &Blocks, BlockSet& Entries) {
      if (Entries.size() == 1) {
        Block *Curr = *(Entries.begin());
        if (Curr->BranchesIn.size() == 0) {
          // One entry, no looping ==> Simple
          SimpleShape *Ret = new SimpleShape(Curr);
          Notice(Ret);
          if (Blocks.size() > 1) {
            Blocks.erase(Curr);
            Entries.clear();
            GetBlocksOut(Curr, Entries);
            Ret->Next = Process(Blocks, Entries);
          }
          return Ret;
        }
        // One entry, looping ==> Loop
        return MakeLoop(Blocks, Entries);
      }
      assert(0);
      return NULL;
      /*
      // More than one entry, try to eliminate through a Multiple groups of
      // independent blocks from an entry/ies
      BlockBlockVec IndependentGroups;
      FindIndependentGroups(Blocks, Entries, IndependentGroups);
      if (IndependentGroups.size() > 0) {
        // Independent groups removable ==> Multiple
        return MakeMultiple(Blocks, Entries, IndependentGroups);
      }
      // No independent groups, must be loopable ==> Loop
      return MakeLoop(Blocks, Entries);
      */
    }
  };

  BlockSet AllBlocks;
  for (int i = 0; i < Blocks.size(); i++) {
    AllBlocks.insert(Blocks[i]);
  }
  BlockSet Entries;
  Entries.insert(Entry);
  Root = Recursor(this).Process(AllBlocks, Entries);
}

void Debugging::Dump(BlockSet &Blocks, const char *prefix) {
  printf("Dumping BlockSet%s%s, size %d\n", prefix ? " " : "", prefix ? prefix : "", Blocks.size());
  for (BlockSet::iterator iter = Blocks.begin(); iter != Blocks.end(); iter++) {
    printf("  %d\n", (*iter)->Id);
  }
}

