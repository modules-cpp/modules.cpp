// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
#include <cstddef>
#include <span>
#include <string_view>

import mm.shell;
import mm.test;

namespace {

void dummy_handler(
    void* ctx,
    std::span<const std::string_view> args,
    mm::shell::CommandContext& context,
    mm::shell::CommandResult& result) {
    (void)context;
    if (ctx != nullptr) {
        *static_cast<int*>(ctx) = static_cast<int>(args.size());
    }
    result.flow = mm::shell::Flow::Normal;
    result.status = 0;
}

void registry_install_and_find() {
    mm::shell::CommandDescriptor storage[4];
    mm::shell::Registry reg(storage);

    mm::test::expect(reg.count() == 0, "initial count zero");
    mm::test::expect(reg.capacity() == 4, "capacity four");

    const mm::shell::CommandDescriptor desc{
        .name = "test_cmd",
        .summary = "A test command",
        .handler = &dummy_handler,
    };

    const auto res = reg.install(desc);
    mm::test::expect(res.ok(), "install ok");
    mm::test::expect(res == mm::shell::Status::Ok, "res == Status::Ok");
    mm::test::expect(reg.count() == 1, "count one");

    const auto* found = reg.find("test_cmd");
    mm::test::expect(found != nullptr, "found installed command");
    if (found != nullptr) {
        mm::test::expect(found->name == "test_cmd", "name matches");
        mm::test::expect(found->summary == "A test command", "summary matches");
    }

    mm::test::expect(reg.find("missing") == nullptr, "missing not found");
}

void registry_rejects_duplicates() {
    mm::shell::CommandDescriptor storage[4];
    mm::shell::Registry reg(storage);

    const mm::shell::CommandDescriptor desc{
        .name = "dup",
        .summary = "A command",
        .handler = &dummy_handler,
    };

    mm::test::expect(reg.install(desc).ok(), "first dup ok");
    const auto res = reg.install(desc);
    mm::test::expect(res.status == mm::shell::Status::Duplicate,
                     "second dup rejected as Duplicate");
    mm::test::expect(reg.count() == 1, "count still one");
}

void registry_capacity_limit() {
    mm::shell::CommandDescriptor storage[2];
    mm::shell::Registry reg(storage);

    const mm::shell::CommandDescriptor d1{
        .name = "cmd1",
        .handler = &dummy_handler,
    };
    const mm::shell::CommandDescriptor d2{
        .name = "cmd2",
        .handler = &dummy_handler,
    };
    const mm::shell::CommandDescriptor d3{
        .name = "cmd3",
        .handler = &dummy_handler,
    };

    mm::test::expect(reg.install(d1).ok(), "d1 ok");
    mm::test::expect(reg.install(d2).ok(), "d2 ok");

    const auto res = reg.install(d3);
    mm::test::expect(res.status == mm::shell::Status::Overflow,
                     "d3 rejected as Overflow");
    mm::test::expect(
        res.overflow.storage_class == mm::shell::StorageClass::CustomCommands,
        "overflow storage class is CustomCommands");
    mm::test::expect(res.overflow.required == 3,
                     "overflow required is 3");
    mm::test::expect(reg.count() == 2, "count stays two");
}

void registry_install_pack_all_or_none() {
    mm::shell::CommandDescriptor storage[4];
    mm::shell::Registry reg(storage);

    const mm::shell::CommandDescriptor good{
        .name = "initial",
        .handler = &dummy_handler,
    };
    (void)reg.install(good);

    // Pack with duplicate name conflicting with existing
    const mm::shell::CommandDescriptor bad_pack[] = {
        { .name = "pack1", .handler = &dummy_handler },
        { .name = "initial", .handler = &dummy_handler },
    };

    const auto bad_status = reg.install_pack(bad_pack);
    mm::test::expect(bad_status.status == mm::shell::Status::Duplicate,
                     "conflicting pack rejected as Duplicate");
    mm::test::expect(reg.count() == 1,
                     "count stays 1; pack was not committed");
    mm::test::expect(reg.find("pack1") == nullptr, "pack1 was rolled back");

    // Pack with internal duplicate within pack
    const mm::shell::CommandDescriptor internal_dup[] = {
        { .name = "same", .handler = &dummy_handler },
        { .name = "same", .handler = &dummy_handler },
    };
    const auto dup_status = reg.install_pack(internal_dup);
    mm::test::expect(dup_status.status == mm::shell::Status::Duplicate,
                     "internal duplicate in pack rejected");
    mm::test::expect(reg.count() == 1, "count stays 1");

    // Pack exceeding capacity
    const mm::shell::CommandDescriptor huge_pack[] = {
        { .name = "p1", .handler = &dummy_handler },
        { .name = "p2", .handler = &dummy_handler },
        { .name = "p3", .handler = &dummy_handler },
        { .name = "p4", .handler = &dummy_handler },
    };
    const auto huge_status = reg.install_pack(huge_pack);
    mm::test::expect(huge_status.status == mm::shell::Status::Overflow,
                     "huge pack rejected as Overflow");
    mm::test::expect(
        huge_status.overflow.storage_class ==
            mm::shell::StorageClass::CustomCommands,
        "overflow storage class is CustomCommands");
    mm::test::expect(huge_status.overflow.required == 5,
                     "overflow required is 5");
    mm::test::expect(reg.count() == 1, "count stays 1");

    // Pack that fits
    const mm::shell::CommandDescriptor good_pack[] = {
        { .name = "pack1", .handler = &dummy_handler },
        { .name = "pack2", .handler = &dummy_handler },
    };
    const auto good_status = reg.install_pack(good_pack);
    mm::test::expect(good_status.ok(), "good pack ok");
    mm::test::expect(reg.count() == 3, "count is now three");
    mm::test::expect(reg.find("pack1") != nullptr, "pack1 found");
    mm::test::expect(reg.find("pack2") != nullptr, "pack2 found");
}

void registry_rejects_invalid_names() {
    mm::shell::CommandDescriptor storage[4];
    mm::shell::Registry reg(storage);

    const std::string_view bad_names[] = {
        "", "a b", "a\tb", "a/b", "cmd|pipe", "cmd;semi", "cmd&bg",
        "cmd>out", "cmd<in", "cmd$var", "cmd`sub`", "cmd\\esc",
        "cmd\"quote", "cmd'quote", "cmd*glob", "cmd?glob", "cmd[b]",
        "cmd#hash", "cmd~home", "cmd=eq", "cmd!bang",
    };

    for (const auto name : bad_names) {
        const mm::shell::CommandDescriptor desc{
            .name = name,
            .handler = &dummy_handler,
        };
        const auto res = reg.install(desc);
        mm::test::expect(res.status == mm::shell::Status::BadArgument,
                         "invalid command name rejected");
    }
    mm::test::expect(reg.count() == 0, "no invalid names installed");

    const mm::shell::CommandDescriptor bracket{
        .name = "[",
        .summary = "Embedded test builtin",
        .command_class = mm::shell::CommandClass::Builtin,
        .handler = &dummy_handler,
    };
    mm::test::expect(reg.install(bracket).ok(), "exact [ name is admitted");
    mm::test::expect(reg.find("[") != nullptr, "[ command is registered");
}

void registry_rejects_public_special_builtin() {
    mm::shell::CommandDescriptor storage[4];
    mm::shell::Registry reg(storage);

    const mm::shell::CommandDescriptor special_desc{
        .name = "special_cmd",
        .command_class = mm::shell::CommandClass::SpecialBuiltin,
        .handler = &dummy_handler,
    };

    // Public install rejects SpecialBuiltin
    const auto pub_res = reg.install(special_desc);
    mm::test::expect(pub_res.status == mm::shell::Status::BadArgument,
                     "public install rejects SpecialBuiltin");
    mm::test::expect(reg.count() == 0, "count remains 0");

    // Public install_pack rejects SpecialBuiltin
    const mm::shell::CommandDescriptor pack[] = { special_desc };
    const auto pack_res = reg.install_pack(pack);
    mm::test::expect(pack_res.status == mm::shell::Status::BadArgument,
                     "public install_pack rejects SpecialBuiltin");
    mm::test::expect(reg.count() == 0, "count remains 0");

}

void two_independent_registries() {
    mm::shell::CommandDescriptor s1[2];
    mm::shell::CommandDescriptor s2[2];
    mm::shell::Registry reg1(s1);
    mm::shell::Registry reg2(s2);

    const mm::shell::CommandDescriptor d1{
        .name = "only_reg1",
        .handler = &dummy_handler,
    };
    (void)reg1.install(d1);

    mm::test::expect(reg1.count() == 1, "reg1 count 1");
    mm::test::expect(reg2.count() == 0, "reg2 count 0");
    mm::test::expect(reg1.find("only_reg1") != nullptr, "reg1 has only_reg1");
    mm::test::expect(reg2.find("only_reg1") == nullptr, "reg2 lacks only_reg1");
}

const mm::test::case_ cases[] = {
    { "registry install and find", &registry_install_and_find },
    { "registry rejects duplicates", &registry_rejects_duplicates },
    { "registry capacity limit", &registry_capacity_limit },
    { "registry install pack all or none", &registry_install_pack_all_or_none },
    { "registry rejects invalid names", &registry_rejects_invalid_names },
    { "registry rejects public special builtin",
      &registry_rejects_public_special_builtin },
    { "two independent registries", &two_independent_registries },
};

const mm::test::registrar reg{"mm.shell registry", cases};

}  // namespace
