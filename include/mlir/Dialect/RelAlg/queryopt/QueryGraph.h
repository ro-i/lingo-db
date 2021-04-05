#ifndef DB_DIALECTS_QUERYGRAPH_H
#define DB_DIALECTS_QUERYGRAPH_H

#include "dynamic_bitset.h"
#include "llvm/Support/Debug.h"
#include <llvm/ADT/TypeSwitch.h>
#include <functional>
#include <iostream>
#include <unordered_set>
#include <mlir/Dialect/RelAlg/IR/RelAlgOps.h>

namespace mlir::relalg {
class QueryGraph {
   public:
   struct hash_dyn_bitset {
      size_t operator()(const sul::dynamic_bitset<>& bitset) const {
         size_t res = 0;
         for (size_t i = 0; i < bitset.num_blocks(); i++) {
            res ^= bitset.data()[i];
         }
         return res;
      }
   };
   using attribute_set = llvm::SmallPtrSet<mlir::relalg::RelationalAttribute*, 8>;

   using node_set = sul::dynamic_bitset<>;
   size_t num_nodes;

   class NodeResolver {
      QueryGraph& qg;

      std::unordered_map<relalg::RelationalAttribute*, size_t> attr_to_nodes;

      public:
      NodeResolver(QueryGraph& qg) : qg(qg) {}

      void add(relalg::RelationalAttribute* attr, size_t nodeid) {
         attr_to_nodes[attr] = nodeid;
      }

      void merge(const NodeResolver& other) {
         for (auto x : other.attr_to_nodes) {
            auto [attr, nodeid] = x;
            if (attr_to_nodes.count(attr)) {
               auto currid = attr_to_nodes[attr];
               if (qg.nodes[nodeid].op->isBeforeInBlock(qg.nodes[currid].op.getOperation())) {
                  currid = nodeid;
               }
               attr_to_nodes[attr] = currid;
            } else {
               attr_to_nodes[attr] = nodeid;
            }
         }
      }

      size_t resolve(relalg::RelationalAttribute* attr) {
         return attr_to_nodes[attr];
      }
   };

   struct Edge {
      Operator op;
      std::vector<Operator> additional_predicates;
      bool implicit_edge;
      node_set right;
      node_set left;
   };

   struct Node {
      size_t id;
      Operator op;
      std::vector<Operator> additional_predicates;
      size_t cardinality;
      node_set dependencies;

      Node(Operator op) : op(op) {}

      std::vector<size_t> edges;
   };

   std::vector<Node> nodes;
   std::vector<Edge> edges;
   std::unordered_map<node_set, std::vector<size_t>, hash_dyn_bitset> available_edges;

   //std::unordered_map<relalg::RelationalAttribute *, size_t> attr_to_nodes;

   QueryGraph(size_t num_nodes, std::unordered_set<mlir::Operation*>& already_optimized) : num_nodes(num_nodes), already_optimized(already_optimized) {}

   size_t addNode(Operator op) {
      Node n = Node(op);
      n.id = nodes.size();
      nodes.push_back(n);
      node_for_op[op.getOperation()] = n.id;
      return n.id;
   }

   void print_readable(node_set S, llvm::raw_ostream& out) {
      out << "{";
      iterateNodes(S, [&](Node& n) { out << n.id << ","; });
      out << "}";
   }

   void print(llvm::raw_ostream& out) {
      out << "QueryGraph:{\n";
      out << "Nodes: [\n";
      for (auto& n : nodes) {
         out << "{" << n.id << ",";
         n.op->print(out);
         out << ", predicates={";
         for (auto op : n.additional_predicates) {
            op->print(out);
            out << ",";
         }

         out << "}}";
         out << "},\n";
      }
      out << "]\n";
      out << "Edges: [\n";
      for (auto& e : edges) {
         out << "{ v=";
         print_readable(e.left, out);
         out << ", u=";
         print_readable(e.right, out);
         out << ", op=\n";
         if (e.op) {
            e.op->print(out);
         }
         out << ", predicates={";
         for (auto op : e.additional_predicates) {
            op->print(out);
            out << ",";
         }

         out << "}";
         out << "},\n";
      }
      out << "]\n";
      out << "}\n";
   }

   void dump() {
      print(llvm::dbgs());
   }

   void addEdge(node_set left, node_set right, Operator op, bool implicit_edge) {
      size_t edgeid = edges.size();
      edges.push_back(Edge());
      Edge& e = edges.back();
      if (op) {
         e.op = op;
      }
      e.implicit_edge = implicit_edge;
      e.left = left;
      e.right = right;
      left.iterate_bits_on([&](size_t n) { nodes[n].edges.push_back(edgeid); });
      right.iterate_bits_on([&](size_t n) { nodes[n].edges.push_back(edgeid); });

      available_edges[left | right].push_back(edgeid);
   }

   void iterateNodes(std::function<void(Node&)> fn) {
      iterateNodesDesc(fn);
   }

   void iterateNodesDesc(std::function<void(Node&)> fn) {
      for (auto it = nodes.rbegin(); it != nodes.rend(); it++) {
         fn(*it);
      }
   }
   mlir::relalg::QueryGraph::node_set nodeSet(const std::vector<size_t>& nodes) {
      node_set res(num_nodes);
      for (auto x : nodes) {
         res.set(x);
      }
      return res;
   }

   void iterateSetDec(node_set S, std::function<void(size_t)> fn) {
      std::vector<size_t> positions;
      S.iterate_bits_on([&](size_t v) {
         positions.push_back(v);
      });
      for (auto it = positions.rbegin(); it != positions.rend(); it++) {
         fn(*it);
      }
   }

   bool isConnected(node_set S1, node_set S2) {
      bool found = false;
      S1.iterate_bits_on([&](size_t v) {
         Node& n = nodes[v];
         for (auto edgeid : n.edges) {
            auto& edge = edges[edgeid];
            if (edge.left.is_subset_of(S1) && edge.right.is_subset_of(S2)) {
               found = true;
            }
            if (edge.left.is_subset_of(S2) && edge.right.is_subset_of(S1)) {
               found = true;
            }
         }
      });
      return found;
   }

   void iterateNodes(node_set S, std::function<void(Node&)> fn) {
      S.iterate_bits_on([&](size_t v) {
         Node& n = nodes[v];
         fn(n);
      });
   }

   void iterateEdges(node_set S, std::function<void(Edge&)> fn) {
      iterateNodes(S, [&](Node& n) {
         for (auto edgeid : n.edges) {
            auto& edge = edges[edgeid];
            fn(edge);
         }
      });
   }

   node_set getNeighbors(node_set S, node_set X) {
      node_set res(num_nodes);
      iterateEdges(S, [&](Edge& edge) {
         if (edge.left.is_subset_of(S) && !S.intersects(edge.right) && !X.intersects(edge.right)) {
            res.set(edge.right.find_first());
         } else if (edge.right.is_subset_of(S) && !S.intersects(edge.left) && !X.intersects(edge.left)) {
            res.set(edge.left.find_first());
         }
      });
      return res;
   }

   node_set fill_until(size_t n) {
      auto res = node_set(num_nodes);
      res.set(0, n + 1, true);
      return res;
   }

   node_set negate(node_set S) {
      size_t pos = S.find_first();
      size_t flip_len = num_nodes - pos - 1;
      if (flip_len) {
         S.flip(pos + 1, flip_len);
      }
      return S;
   }

   node_set single(size_t pos) {
      auto res = node_set(num_nodes);
      res.set(pos);
      return res;
   }

   void iterateSubsets(node_set S, std::function<void(node_set)> fn) {
      if (!S.any()) return;
      auto S1 = S & negate(S);
      while (S1 != S) {
         fn(S1);
         auto S1flipped = S1;
         S1flipped.flip();
         auto S2 = S & S1flipped;
         S1 = S & negate(S2);
      }
      fn(S);
   }

   node_set calcSES(Operator op, NodeResolver& resolver) {
      node_set res = node_set(num_nodes);
      for (auto attr : op.getUsedAttributes()) {
         res.set(resolver.resolve(attr));
      }
      return res;
   }

   node_set expand(node_set S) {
      iterateNodes(S, [&](Node& n) {
         if (n.dependencies.size() > 0) {
            S |= n.dependencies;
         }
      });
      return S;
   }

   enum OpType {
      None,
      CP = 1,
      InnerJoin,
      SemiJoin,
      AntiSemiJoin,
      OuterJoin,
      FullOuterJoin,
      LAST
   };
   // @formatter:off
        // clang-format off
         bool assoc[OpType::LAST][OpType::LAST] = {
                /* None =  */{},
                /* CP           =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* InnerJoin    =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* SemiJoin     =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
                /* AntiSemiJoin =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
                /* OuterJoin    =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
                /* FullOuterJoin=  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false}
         };
         bool l_asscom[OpType::LAST][OpType::LAST] = {
                /* None =  */{},
                /* CP           =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* InnerJoin    =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* SemiJoin     =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* AntiSemiJoin =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* OuterJoin    =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/true,/*AntiSemiJoin=*/true,/*OuterJoin=*/true,/*FullOuterJoin=*/false},
                /* FullOuterJoin=  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false}
         };
         bool r_asscom[OpType::LAST][OpType::LAST] = {
            /* None =  */{},
            /* CP           =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
            /* InnerJoin    =  */{/*None=*/false,/*CP=*/true,/*InnerJoin=*/true,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
            /* SemiJoin     =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
            /* AntiSemiJoin =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
            /* OuterJoin    =  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false},
            /* FullOuterJoin=  */{/*None=*/false,/*CP=*/false,/*InnerJoin=*/false,/*SemiJoin=*/false,/*AntiSemiJoin=*/false,/*OuterJoin=*/false,/*FullOuterJoin=*/false}
         };
        // @formatter:on
         // clang-format on
         OpType getOpType(Operator op) {
            return ::llvm::TypeSwitch<mlir::Operation*, OpType>(op.getOperation())
               .Case<mlir::relalg::CrossProductOp>([&](mlir::Operation* op) { return CP; })
               .Case<mlir::relalg::InnerJoinOp>([&](mlir::Operation* op) { return InnerJoin; })
               .Case<mlir::relalg::SemiJoinOp>([&](mlir::Operation* op) { return SemiJoin; })
               .Case<mlir::relalg::AntiSemiJoinOp>([&](mlir::Operation* op) { return AntiSemiJoin; })
               .Case<mlir::relalg::SingleJoinOp>([&](mlir::Operation* op) { return OuterJoin; })
               .Case<mlir::relalg::MarkJoinOp>([&](mlir::Operation* op) { return SemiJoin; }) //todo is this correct?
               .Case<mlir::relalg::OuterJoinOp>([&](mlir::relalg::OuterJoinOp op) {
                  return op.join_direction() == mlir::relalg::JoinDirection::full ? FullOuterJoin : OuterJoin;
               })

               .Default([&](auto x) {
                  return None;
               });
         }
         bool intersects(const attribute_set& a, const attribute_set& b) {
            for (auto x : a) {
               if (b.contains(x)) {
                  return true;
               }
            }
            return false;
         }
         bool is(const bool (&table)[OpType::LAST][OpType::LAST], Operator a, Operator b) {
            return table[getOpType(a)][getOpType(b)];
         }
         std::pair<Operator, Operator> normalizeChildren(Operator op) {
            size_t left = 0;
            size_t right = 1;
            if (op->hasAttr("join_direction")) {
               mlir::relalg::JoinDirection joinDirection = mlir::relalg::symbolizeJoinDirection(
                                                              op->getAttr("join_direction").dyn_cast_or_null<mlir::IntegerAttr>().getInt())
                                                              .getValue();
               if (joinDirection == mlir::relalg::JoinDirection::right) {
                  std::swap(left, right);
               }
            }
            return {op.getChildren()[left], op.getChildren()[right]};
         }
         node_set calcTES(Operator b, NodeResolver& resolver) {
            if (TESs.count(b.getOperation())) {
               return TESs[b.getOperation()];
            } else {
               node_set TES = calcSES(b, resolver);
               auto children = b.getChildren();
               if (children.size() == 2) {
                  auto [b_left, b_right] = normalizeChildren(b);
                  for (auto a : b_left.getAllSubOperators()) {
                     if (a.getChildren().size() == 2) {
                        auto [a_left, a_right] = normalizeChildren(a);
                        if (!is(assoc, a, b)) {
                           TES |= calcT(a_left, resolver);
                        }
                        if (!is(l_asscom, a, b)) {
                           TES |= calcT(a_right, resolver);
                        }
                     } else {
                        if (mlir::isa<mlir::relalg::AggregationOp>(a.getOperation())) {
                           TES |= calcT(a, resolver);
                        }
                     }
                  }
                  for (auto a : b_right.getAllSubOperators()) {
                     if (a.getChildren().size() == 2) {
                        auto [a_left, a_right] = normalizeChildren(a);
                        if (!is(assoc, b, a)) {
                           TES |= calcT(a_right, resolver);
                        }
                        if (!is(r_asscom, b, a)) {
                           TES |= calcT(a_left, resolver);
                        }
                     } else {
                        if (mlir::isa<mlir::relalg::AggregationOp>(a.getOperation())) {
                           TES |= calcT(a, resolver);
                        }
                     }
                  }

               } else if (children.size() == 1) {
                  auto only_child = children[0];
                  if (mlir::isa<mlir::relalg::AggregationOp>(b.getOperation())) {
                     TES |= calcT(only_child, resolver);
                  }
                  if (auto renameop = mlir::dyn_cast_or_null<mlir::relalg::RenamingOp>(b.getOperation())) {
                     for (auto a : only_child.getAllSubOperators()) {
                        if (intersects(a.getUsedAttributes(), renameop.getUsedAttributes()) || intersects(a.getCreatedAttributes(), renameop.getCreatedAttributes())) {
                           TES |= calcT(only_child, resolver);
                        }
                     }
                  }
               }
               TESs[b.getOperation()] = TES;
               return TES;
            }
         }

         std::unordered_map<mlir::Operation*, node_set> Ts;
         std::unordered_map<mlir::Operation*, node_set> TESs;
         std::unordered_map<mlir::Operation*, size_t> node_for_op;
         std::unordered_set<mlir::Operation*>& already_optimized;

         node_set calcT(Operator op, NodeResolver& resolver) {
            if (Ts.count(op.getOperation())) {
               return Ts[op.getOperation()];
            } else {
               node_set init = node_set(num_nodes);
               if (node_for_op.count(op.getOperation())) {
                  init.set(node_for_op[op.getOperation()]);
               }
               if (!already_optimized.count(op.getOperation())) {
                  for (auto child : op.getChildren()) {
                     init |= calcT(child, resolver);
                  }
               }
               Ts[op.getOperation()] = init;
               return init;
            }
         }
         bool canPushSelection(Operator sel, Operator curr, NodeResolver& resolver) {
            if (curr.getChildren().size() == 1) {
               return true;
            }
            auto SES = calcSES(sel, resolver);
            auto TES = calcTES(curr, resolver);
            auto [b_left, b_right] = normalizeChildren(curr);
            node_set left_TES = calcT(b_left, resolver) & TES;
            node_set right_TES = calcT(b_right, resolver) & TES;
            if (left_TES.intersects(SES) && right_TES.intersects(SES)) {
               return false;
            }
            switch (getOpType(curr)) {
               case SemiJoin:
               case AntiSemiJoin:
               case OuterJoin:
                  return !right_TES.intersects(SES);
               case FullOuterJoin: return false;
               default:
                  return true;
            }
         }
         node_set ones() {
            node_set x = mlir::relalg::QueryGraph::node_set(num_nodes);
            x.set();
            return x;
         }
};
}
#endif //DB_DIALECTS_QUERYGRAPH_H
