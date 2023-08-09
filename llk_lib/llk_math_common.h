
#pragma once

#include "ckernel_defs.h"
#include "ckernel_include.h"
#include "cmath_common.h"
#ifdef PERF_DUMP
#include "ckernel_perf_api.h"
#endif

using namespace ckernel::math;

template <DstSync Dst>
inline void llk_math_wait_for_dest_available() {
    TT_LLK_DUMP("llk_math_wait_for_dest_available<{}>()", Dst);
    // These liteweight functions for sync with packer imply
    // no mode change - entire epoch is either double buffer or single buffer
#ifdef PERF_DUMP
    if constexpr(MATH_PACK_DECOUPLE == 0) {
        math_dest_wait();
    }
#else
    math_dest_wait();
#endif
}

template <DstSync Dst = SyncFull, bool is_fp32_dest_acc_en = false>
inline void llk_math_dest_section_done() {
    TT_LLK_DUMP("llk_math_dest_section_done<{}, {}>()", Dst, is_fp32_dest_acc_en);
#ifdef PERF_DUMP
    if constexpr(MATH_PACK_DECOUPLE) {
        return;
    }
#endif

    constexpr uint32_t DEST_NUM_TILES_SHIFT = is_fp32_dest_acc_en ? (1) : (0);
    constexpr uint32_t DEST_NUM_TILES = DEST_NUM_TILES_FP16 >> DEST_NUM_TILES_SHIFT;

    set_math_semaphores();
    if constexpr ((Dst == DstSync::SyncHalf) || (Dst == DstSync::SyncTile2)) {
        math_sync_tile_dst_index = 0;
        dest_section_flip();
    } else if constexpr (Dst == DstSync::SyncTile16) {
        math_sync_tile_dst_index++;
        math_sync_tile_dst_index &= (DEST_NUM_TILES - 1);
    }
}

template <DstSync Dst, bool is_fp32_dest_acc_en = false>
inline void llk_math_pack_sync_init() {
    TT_LLK_DUMP("llk_math_pack_sync_init<{}, {}>()", Dst, is_fp32_dest_acc_en);
#ifdef PERF_DUMP
    if constexpr(MATH_PACK_DECOUPLE) {
        return;
    }
#endif
    tensix_sync();
    while (semaphore_read(semaphore::MATH_PACK) > 0) {
    };  // Wait for previous packs to finish before claiming all dest
    if constexpr (Dst == DstSync::SyncFull) {
        TTI_SEMINIT(1, 0, p_stall::SEMAPHORE_1);
        reset_dest_offset_id();
        set_dest_section_base<StartZero>();
    } else if constexpr (Dst == DstSync::SyncHalf) {
        TTI_SEMINIT(2, 0, p_stall::SEMAPHORE_1);
        reset_dest_offset_id();
        set_dest_section_base<StartZero>();
    } else if constexpr (Dst == DstSync::SyncTile2) {
        TTI_SEMINIT(2, 0, p_stall::SEMAPHORE_1);
        reset_dest_offset_id();
        set_dest_section_base<StartZero>();
        math_sync_tile_dst_index = 0;
    } else {
        static_assert(Dst == DstSync::SyncTile16);

        constexpr uint32_t DEST_NUM_TILES_SHIFT = is_fp32_dest_acc_en ? (1) : (0);
        constexpr uint32_t DEST_NUM_TILES = DEST_NUM_TILES_FP16 >> DEST_NUM_TILES_SHIFT;
        constexpr uint32_t SEM_INIT_MAX = (DEST_NUM_TILES < 15) ? DEST_NUM_TILES : 15;

        TTI_SEMINIT(SEM_INIT_MAX, 0, p_stall::SEMAPHORE_1);
        reset_dest_offset_id();
        set_dest_section_base<StartZero>();
        math_sync_tile_dst_index = 0;
    }
}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_math_get_tile(std::uint32_t operand, std::uint32_t tile_index, std::uint32_t* p_tile) {
    TT_LLK_DUMP("llk_math_get_tile<{}, {}>({}, {}, tile_pointer)", mail2math, mail2pack, operand, tile_index);
    if constexpr (mail2math) {
       *p_tile = mailbox_read(ThreadId::UnpackThreadId);
    } else {
       *p_tile = 0x0;
    }

}

template <bool mail2math=true, bool mail2pack=true>
inline void llk_math_release_tile(std::uint32_t operand) {
    TT_LLK_DUMP("llk_math_release_tile<{}, {}>({})", mail2math, mail2pack, operand);
    if constexpr (mail2math) {
       semaphore_get(semaphore::UNPACK_OPERAND_SYNC);
    }   
}

inline void llk_math_debug_dump(std::uint8_t *data, std::uint32_t byte_size) {
    TT_LLK_DUMP("llk_math_debug_dump(ptr, {})", byte_size);
    debug_dump(data, byte_size);
}

inline void llk_math_debug_dump_seek(std::uint8_t offset) {
    debug_dump_seek(offset);
}

inline void llk_math_reconfig_data_format(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand, const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format({}, {}, {}, {})", srca_old_operand, srca_new_operand, srcb_old_operand, srcb_new_operand);
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id]) && (unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])) {
        uint config_data = (unpack_dst_format[new_srca_operand_id] << ALU_FORMAT_SPEC_REG0_SrcA_SHAMT) | (unpack_dst_format[new_srcb_operand_id] << ALU_FORMAT_SPEC_REG1_SrcB_SHAMT);
        constexpr uint config_mask = ALU_FORMAT_SPEC_REG0_SrcA_MASK | ALU_FORMAT_SPEC_REG1_SrcB_MASK;
        cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG0_SrcA_ADDR32, 0, config_mask>(config_data);

    } else if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id])){
        cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG0_SrcA_RMW>((uint)unpack_dst_format[new_srca_operand_id]);
    } else if((unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])){
        cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG1_SrcB_RMW>((uint)unpack_dst_format[new_srcb_operand_id]);
    }
}

inline void llk_math_reconfig_data_format(const std::uint32_t srca_new_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format({}, {})", srca_new_operand, srcb_new_operand);

    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    uint config_data = (unpack_dst_format[new_srca_operand_id] << ALU_FORMAT_SPEC_REG0_SrcA_SHAMT) | (unpack_dst_format[new_srcb_operand_id] << ALU_FORMAT_SPEC_REG1_SrcB_SHAMT);
    constexpr uint config_mask = ALU_FORMAT_SPEC_REG0_SrcA_MASK | ALU_FORMAT_SPEC_REG1_SrcB_MASK;
    cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG0_SrcA_ADDR32, 0, config_mask>(config_data);
}

inline void llk_math_reconfig_data_format_srca(const std::uint32_t srca_old_operand, const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srca({}, {})", srca_old_operand, srca_new_operand);
    std::uint32_t old_srca_operand_id = get_operand_id(srca_old_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);

    if((unpack_dst_format[old_srca_operand_id] != unpack_dst_format[new_srca_operand_id])){
        cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG0_SrcA_RMW>((uint)unpack_dst_format[new_srca_operand_id]);
    }
}

inline void llk_math_reconfig_data_format_srca(const std::uint32_t srca_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srca({})", srca_new_operand);
    std::uint32_t new_srca_operand_id = get_operand_id(srca_new_operand);
    cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG0_SrcA_RMW>((uint)unpack_dst_format[new_srca_operand_id]);
}

inline void llk_math_reconfig_data_format_srcb(const std::uint32_t srcb_old_operand, const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srcb({}, {})", srcb_old_operand, srcb_new_operand);
    std::uint32_t old_srcb_operand_id = get_operand_id(srcb_old_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);

    if((unpack_dst_format[old_srcb_operand_id] != unpack_dst_format[new_srcb_operand_id])){
        cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG1_SrcB_RMW>((uint)unpack_dst_format[new_srcb_operand_id]);
    }
}

inline void llk_math_reconfig_data_format_srcb(const std::uint32_t srcb_new_operand) {
    TT_LLK_DUMP("llk_math_reconfig_data_format_srcb({})", srcb_new_operand);
    std::uint32_t new_srcb_operand_id = get_operand_id(srcb_new_operand);
    cfg_reg_rmw_tensix<ALU_FORMAT_SPEC_REG1_SrcB_RMW>((uint)unpack_dst_format[new_srcb_operand_id]);
}
