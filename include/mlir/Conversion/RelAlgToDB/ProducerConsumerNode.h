#ifndef MLIR_CONVERSION_RELALGTODB_PRODUCERCONSUMERNODE_H
#define MLIR_CONVERSION_RELALGTODB_PRODUCERCONSUMERNODE_H

#include "llvm/ADT/ScopedHashTable.h"

#include "mlir/Dialect/RelAlg/IR/RelAlgOps.h"
#include "mlir/Dialect/RelAlg/IR/RelationalAttribute.h"
#include "mlir/IR/Builders.h"
#include "mlir/Pass/Pass.h"
#include <memory>

namespace mlir {
namespace relalg {
class LoweringContext {
   llvm::ScopedHashTable<const mlir::relalg::RelationalAttribute*, mlir::Value> symbolTable;

   public:
   using AttributeResolverScope = llvm::ScopedHashTableScope<const mlir::relalg::RelationalAttribute*, mlir::Value>;

   mlir::Value getValueForAttribute(const mlir::relalg::RelationalAttribute* attribute) const {
      assert(symbolTable.count(attribute));
      return symbolTable.lookup(attribute);
   }
   void setValueForAttribute(AttributeResolverScope& scope, const mlir::relalg::RelationalAttribute* iu, mlir::Value v) {
      symbolTable.insertIntoScope(&scope, iu, v);
   }
   AttributeResolverScope createScope() {
      return AttributeResolverScope(symbolTable);
   }
   mlir::Value executionContext;
   std::unordered_map<size_t, mlir::Value> builders;
   size_t getBuilderId() {
      static size_t id = 0;
      return id++;
   }
};
class ProducerConsumerBuilder : public mlir::OpBuilder {
   public:
   using mlir::OpBuilder::OpBuilder;

   void mergeRelatinalBlock(mlir::Block* source, LoweringContext& context, LoweringContext::AttributeResolverScope& scope) {
      mlir::Block* dest = getBlock();

      // Splice the operations of the 'source' block into the 'dest' block and erase
      // it.
      llvm::iplist<mlir::Operation> translated;
      std::vector<mlir::Operation*> toErase;
      for (auto getAttrOp : source->getOps<mlir::relalg::GetAttrOp>()) {
         getAttrOp.replaceAllUsesWith(context.getValueForAttribute(&getAttrOp.attr().getRelationalAttribute()));
         toErase.push_back(getAttrOp.getOperation());
      }
      for (auto addAttrOp : source->getOps<mlir::relalg::AddAttrOp>()) {
         context.setValueForAttribute(scope, &addAttrOp.attr().getRelationalAttribute(), addAttrOp.val());
         toErase.push_back(addAttrOp.getOperation());
      }

      dest->getOperations().splice(dest->end(), source->getOperations());
      for (auto* op : toErase) {
         op->erase();
      }
   }
};
class ProducerConsumerNode {
   protected:
   ProducerConsumerNode* consumer;
   std::vector<std::unique_ptr<ProducerConsumerNode>> children;
   std::vector<size_t> requiredBuilders;
   mlir::relalg::Attributes requiredAttributes;
   void propagateInfo() {
      for (auto& c : children) {
         auto available = c->getAvailableAttributes();
         mlir::relalg::Attributes toPropagate = requiredAttributes.intersect(available);
         c->setInfo(this, toPropagate);
      }
   }
   std::vector<mlir::Value> getRequiredBuilderValues(LoweringContext& context) {
      std::vector<mlir::Value> res;
      for (auto x : requiredBuilders) {
         res.push_back(context.builders[x]);
      }
      return res;
   }
   void setRequiredBuilderValues(LoweringContext& context, mlir::ValueRange values) {
      size_t i = 0;
      for (auto x : requiredBuilders) {
         context.builders[x] = values[i++];
      }
   }
   std::vector<mlir::Type> getRequiredBuilderTypes(LoweringContext& context) {
      std::vector<mlir::Type> res;
      for (auto x : requiredBuilders) {
         res.push_back(context.builders[x].getType());
      }
      return res;
   }

   public:
   ProducerConsumerNode(mlir::ValueRange children);
   virtual void addRequiredBuilders(std::vector<size_t> requiredBuilders) {
      this->requiredBuilders.insert(this->requiredBuilders.end(),requiredBuilders.begin(),requiredBuilders.end());
      for (auto& child : children) {
         child->addRequiredBuilders(requiredBuilders);
      }
   }
   virtual void setInfo(ProducerConsumerNode* consumer, mlir::relalg::Attributes requiredAttributes) = 0;
   virtual mlir::relalg::Attributes getAvailableAttributes() = 0;
   virtual void consume(ProducerConsumerNode* child, ProducerConsumerBuilder& builder, LoweringContext& context) = 0;
   virtual void produce(LoweringContext& context, ProducerConsumerBuilder& builder) = 0;
   virtual void done() {}
   virtual ~ProducerConsumerNode() {}
};
class ProducerConsumerNodeRegistry {
   static bool registeredBaseTableOp;
   static bool registeredConstRelOp;
   static bool registeredMaterializeOp;
   static bool registeredSelectionOp;
   static bool registeredMapOp;
   static bool registeredCrossProductOp;
   static bool registeredSortOp;
   static bool registeredAggregationOp;
   static bool registeredInnerJoinOp;
   static bool registeredSemiJoinOp;
   std::unordered_map<std::string, std::function<std::unique_ptr<mlir::relalg::ProducerConsumerNode>(mlir::Operation*)>> nodes;
   ProducerConsumerNodeRegistry() {
      bool res = true;
      res &= registeredBaseTableOp;
      res &= registeredConstRelOp;
      res &= registeredMaterializeOp;
      res &= registeredSelectionOp;
      res &= registeredMapOp;
      res &= registeredCrossProductOp;
      res &= registeredSortOp;
      res &= registeredAggregationOp;
      res &= registeredInnerJoinOp;
//      res &= registeredSemiJoinOp;

   }

   public:
   static ProducerConsumerNodeRegistry& getRegistry() {
      static ProducerConsumerNodeRegistry registry;
      return registry;
   }
   template <typename FnT, typename T = typename llvm::function_traits<std::decay_t<FnT>>::template arg_t<0>>
   static bool registerNode(FnT&& callBack) {
      std::string x = T::getOperationName().str();
      llvm::dbgs() << "inserting:" << x << "\n";
      getRegistry().nodes.insert({x, [callBack = callBack](mlir::Operation* op) { return callBack(mlir::cast<T>(op)); }});
      return true;
   }
   static std::unique_ptr<mlir::relalg::ProducerConsumerNode> createNode(mlir::Operation* operation) {
      std::string opName = operation->getName().getStringRef().str();
      llvm::dbgs() << "looking up:" << opName << "\n";
      if (getRegistry().nodes.count(opName)) {
         return getRegistry().nodes[opName](operation);
      } else {
         return std::unique_ptr<mlir::relalg::ProducerConsumerNode>();
      }
   }
};
} // end namespace relalg
} // end namespace mlir

#endif // MLIR_CONVERSION_RELALGTODB_PRODUCERCONSUMERNODE_H