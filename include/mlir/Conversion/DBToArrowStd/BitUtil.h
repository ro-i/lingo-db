#ifndef MLIR_CONVERSION_DBTOARROWSTD_BITUTIL_H
#define MLIR_CONVERSION_DBTOARROWSTD_BITUTIL_H
#include "FunctionRegistry.h"
#include "mlir/Dialect/DB/IR/DBOps.h"
#include "mlir/Dialect/util/UtilOps.h"

namespace mlir::db::codegen {
class BitUtil {
   public:
   static Value getBit(OpBuilder builder,Location loc, Value bits, Value pos, bool negated = false) {
      auto i1Type = IntegerType::get(builder.getContext(), 1);
      auto i8Type = IntegerType::get(builder.getContext(), 8);

      auto indexType = IndexType::get(builder.getContext());
      Value const3 = builder.create<arith::ConstantOp>(loc, indexType, builder.getIntegerAttr(indexType, 3));
      Value const7 = builder.create<arith::ConstantOp>(loc, indexType, builder.getIntegerAttr(indexType, 7));
      Value const1Byte = builder.create<arith::ConstantOp>(loc, i8Type, builder.getIntegerAttr(i8Type, 1));

      Value div8 = builder.create<arith::ShRUIOp>(loc, indexType, pos, const3);
      Value rem8 = builder.create<arith::AndIOp>(loc, indexType, pos, const7);
      Value loadedByte = builder.create<mlir::util::LoadOp>(loc,i8Type, bits, div8);
      Value rem8AsByte = builder.create<arith::IndexCastOp>(loc, rem8, i8Type);
      Value shifted = builder.create<arith::ShRUIOp>(loc, i8Type, loadedByte, rem8AsByte);
      Value res1 = shifted;
      if (negated) {
         Value constTrue = builder.create<arith::ConstantOp>(loc, i8Type, builder.getIntegerAttr(i8Type, 1));
         res1 = builder.create<arith::XOrIOp>(loc, res1, constTrue); //negate
      }
      Value anded = builder.create<arith::AndIOp>(loc, i8Type, res1, const1Byte);
      Value res = builder.create<arith::TruncIOp>(loc, i1Type, anded);
      return res;
   }
};
} // namespace mlir::db::codegen
#endif // MLIR_CONVERSION_DBTOARROWSTD_BITUTIL_H
