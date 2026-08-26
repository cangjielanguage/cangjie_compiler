// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
// ASTKIND(ast kind, ast value for human reading, ast node, ast size for notify checking related code)
#ifdef ASTKIND
ASTKIND(ANNOTATION, "annotation", Annotation, 520)
ASTKIND(MODIFIER, "modifier", Modifier, 272)
ASTKIND(DECL, "*decl", Decl, 736) // begin of subtypes of Decl.
ASTKIND(MAIN_DECL, "main_decl", MainDecl, 760)
ASTKIND(FUNC_DECL, "func_decl", FuncDecl, 880)
ASTKIND(MACRO_DECL, "macro_decl", MacroDecl, 752)
ASTKIND(CLASS_LIKE_DECL, "class_like_decl", ClassLikeDecl, 832) // begin: subtypes of ClassLikeDecl and InheritableDecl.
ASTKIND(CLASS_DECL, "class_decl", ClassDecl, 848)
ASTKIND(INTERFACE_DECL, "interface_decl", InterfaceDecl, 840) // end: subtypes of ClassLikeDecl.
ASTKIND(EXTEND_DECL, "extend_decl", ExtendDecl, 896)
ASTKIND(ENUM_DECL, "enum_decl", EnumDecl, 936)
ASTKIND(STRUCT_DECL, "struct_decl", StructDecl, 800) // end of subtypes of InheritableDecl.
ASTKIND(TYPE_ALIAS_DECL, "type_alias_decl", TypeAliasDecl, 768)
ASTKIND(PRIMARY_CTOR_DECL, "primary_ctor_decl", PrimaryCtorDecl, 752)
ASTKIND(BUILTIN_DECL, "builtin_decl", BuiltInDecl, 768)
ASTKIND(VAR_DECL, "var_decl", VarDecl, 816) // begin of subtypes of VarDecl and VarDeclAbstract.
ASTKIND(PROP_DECL, "prop_decl", PropDecl, 904)
ASTKIND(MACRO_EXPAND_PARAM, "macro_expand_param", MacroExpandParam, 1656)
ASTKIND(THIS_PARAM, "this_param", ThisParam, 896)
ASTKIND(FUNC_PARAM, "func_param", FuncParam, 880)                                // end of subtypes of VarDecl.
ASTKIND(VAR_WITH_PATTERN_DECL, "var_with_pattern_decl", VarWithPatternDecl, 808) // end of subtypes of VarDeclAbstract.
ASTKIND(GENERIC_PARAM_DECL, "generic_param_decl", GenericParamDecl, 760)
ASTKIND(PACKAGE_DECL, "package_decl", PackageDecl, 744)
ASTKIND(MACRO_EXPAND_DECL, "macro_expand_decl", MacroExpandDecl, 1512)
ASTKIND(INVALID_DECL, "invalid_decl", InvalidDecl, 736) // end of subtypes of Decl.
ASTKIND(PATTERN, "*pattern", Pattern, 272)              // begin of subTypes of Pattern.
ASTKIND(VAR_PATTERN, "var_pattern", VarPattern, 288)
ASTKIND(CONST_PATTERN, "const_pattern", ConstPattern, 288)
ASTKIND(TUPLE_PATTERN, "tuple_pattern", TuplePattern, 368)
ASTKIND(ENUM_PATTERN, "enum_pattern", EnumPattern, 376)
ASTKIND(VAR_OR_ENUM_PATTERN, "var_or_enum_pattern", VarOrEnumPattern, 360)
ASTKIND(TYPE_PATTERN, "type_pattern", TypePattern, 336)
ASTKIND(EXCEPT_TYPE_PATTERN, "except_type_pattern", ExceptTypePattern, 368)
ASTKIND(COMMAND_TYPE_PATTERN, "effect_type_pattern", CommandTypePattern, 368)
ASTKIND(WILDCARD_PATTERN, "wildcard_pattern", WildcardPattern, 272)
ASTKIND(INVALID_PATTERN, "invalid_pattern", InvalidPattern, 272) // end of subtypes of Pattern.
ASTKIND(TYPE, "*type", Type, 440)                                // begin of subtypes of Type.
ASTKIND(REF_TYPE, "ref_type", RefType, 624)
ASTKIND(QUALIFIED_TYPE, "qualified_type", QualifiedType, 632)
ASTKIND(OPTION_TYPE, "option_type", OptionType, 488)
ASTKIND(CONSTANT_TYPE, "constant_type", ConstantType, 472)
ASTKIND(VARRAY_TYPE, "varray_type", VArrayType, 520)
ASTKIND(PRIMITIVE_TYPE, "primitive_type", PrimitiveType, 480)
ASTKIND(PAREN_TYPE, "paren_type", ParenType, 496)
ASTKIND(FUNC_TYPE, "func_type", FuncType, 544)
ASTKIND(TUPLE_TYPE, "tuple_type", TupleType, 528)
ASTKIND(THIS_TYPE, "this_type", ThisType, 440)
ASTKIND(INVALID_TYPE, "invalid_type", InvalidType, 440) // end of subtypes of Type.
ASTKIND(EXPR, "*expr", Expr, 448)                       // begin of subtypes of Expr.
ASTKIND(WILDCARD_EXPR, "wildcard_expr", WildcardExpr, 448)
ASTKIND(CALL_EXPR, "call_expr", CallExpr, 640)
ASTKIND(PAREN_EXPR, "paren_expr", ParenExpr, 496)
ASTKIND(MEMBER_ACCESS, "member_access_expr", MemberAccess, 784) // begin of subtypes of NameReferenceExpr.
ASTKIND(REF_EXPR, "ref_expr", RefExpr, 704)                     // end of subtypes of NameReferenceExpr.
ASTKIND(OPTIONAL_EXPR, "optional_expr", OptionalExpr, 480)
ASTKIND(OPTIONAL_CHAIN_EXPR, "optional_chain_expr", OptionalChainExpr, 448)
ASTKIND(PRIMITIVE_TYPE_EXPR, "primitive_type_expr", PrimitiveTypeExpr, 496)
ASTKIND(RETURN_EXPR, "return_expr", ReturnExpr, 480)
ASTKIND(LIT_CONST_EXPR, "lit_const_expr", LitConstExpr, 560)
ASTKIND(INTERPOLATION_EXPR, "interpolation_expr", InterpolationExpr, 512)
ASTKIND(STR_INTERPOLATION_EXPR, "str_interpolation_expr", StrInterpolationExpr, 528)
ASTKIND(ASSIGN_EXPR, "assign_expr", AssignExpr, 496) // begin of OverloadableExpr.
ASTKIND(UNARY_EXPR, "unary_expr", UnaryExpr, 480)
ASTKIND(BINARY_EXPR, "binary_expr", BinaryExpr, 480)
ASTKIND(INC_OR_DEC_EXPR, "inc_or_dec_expr", IncOrDecExpr, 480)
ASTKIND(SUBSCRIPT_EXPR, "subscript_expr", SubscriptExpr, 544) // end of OverloadableExpr.
ASTKIND(IS_EXPR, "is_expr", IsExpr, 480)
ASTKIND(AS_EXPR, "as_expr", AsExpr, 480)
ASTKIND(RANGE_EXPR, "range_expr", RangeExpr, 528)
ASTKIND(ARRAY_LIT, "array_lit_expr", ArrayLit, 544)
ASTKIND(ARRAY_EXPR, "array_expr", ArrayExpr, 560)
ASTKIND(POINTER_EXPR, "pointer_expr", PointerExpr, 464)
ASTKIND(TUPLE_LIT, "tuple_lit_expr", TupleLit, 544)
ASTKIND(MATCH_EXPR, "match_expr", MatchExpr, 592)
ASTKIND(BLOCK, "block", Block, 528)
ASTKIND(IF_EXPR, "if_expr", IfExpr, 560)
ASTKIND(LET_PATTERN_DESTRUCTOR, "let_pattern_destructor", LetPatternDestructor, 528)
ASTKIND(TOKEN_PART, "token_part", TokenPart, 464)
ASTKIND(QUOTE_EXPR, "quote_expr", QuoteExpr, 528)
ASTKIND(TRY_EXPR, "try_expr", TryExpr, 768)
ASTKIND(WHILE_EXPR, "while_expr", WhileExpr, 528)
ASTKIND(JUMP_EXPR, "jump_expr", JumpExpr, 464)
ASTKIND(EXCLAVE_EXPR, "exclave_expr", ExclaveExpr, 480)
ASTKIND(LAMBDA_EXPR, "lambda_expr", LambdaExpr, 496)
ASTKIND(TRAIL_CLOSURE_EXPR, "trailing_closure_expr", TrailingClosureExpr, 512)
ASTKIND(FOR_IN_EXPR, "for_in_expr", ForInExpr, 576)
ASTKIND(DO_WHILE_EXPR, "do_while_expr", DoWhileExpr, 544)
ASTKIND(TYPE_CONV_EXPR, "type_conv_expr", TypeConvExpr, 544)
ASTKIND(THROW_EXPR, "throw_expr", ThrowExpr, 480)
ASTKIND(PERFORM_EXPR, "perform_expr", PerformExpr, 480)
ASTKIND(RESUME_EXPR, "resume_expr", ResumeExpr, 544)
ASTKIND(SPAWN_EXPR, "spawn_expr", SpawnExpr, 528)
ASTKIND(SYNCHRONIZED_EXPR, "synchronized_expr", SynchronizedExpr, 528)
ASTKIND(MACRO_EXPAND_EXPR, "macro_expand_expr", MacroExpandExpr, 1360)
ASTKIND(IF_AVAILABLE_EXPR, "if_available_expr", IfAvailableExpr, 464)
ASTKIND(INVALID_EXPR, "invalid_expr", InvalidExpr, 480) // end of subtypes of Expr.
ASTKIND(GENERIC, "generic", Generic, 408)
ASTKIND(GENERIC_CONSTRAINT, "generic_constraint", GenericConstraint, 392)
ASTKIND(MATCH_CASE, "match_case", MatchCase, 368)
ASTKIND(MATCH_CASE_OTHER, "match_case_other", MatchCaseOther, 304)
ASTKIND(FUNC_ARG, "func_arg", FuncArg, 424)
ASTKIND(FUNC_PARAM_LIST, "func_param_list", FuncParamList, 360)
ASTKIND(FUNC_BODY, " func_body", FuncBody, 456)
ASTKIND(STRUCT_BODY, "struct_body", StructBody, 336)
ASTKIND(CLASS_BODY, "class_body", ClassBody, 336)
ASTKIND(INTERFACE_BODY, "interface_body", InterfaceBody, 336)
ASTKIND(DUMMY_BODY, "dummy_body", DummyBody, 264)
ASTKIND(IMPORT_CONTENT, "import_content", ImportContent, 608)
ASTKIND(IMPORT_SPEC, "import_spec", ImportSpec, 936)
ASTKIND(PACKAGE_SPEC, "package_spec", PackageSpec, 472)
ASTKIND(PACKAGE, "package", Package, 424)
ASTKIND(FEATURE_ID, "feature_id", FeatureId, 312)
ASTKIND(FEATURES_SET, "features_set", FeaturesSet, 360)
ASTKIND(FEATURES_DIRECTIVE, "features_directive", FeaturesDirective, 320)
ASTKIND(FILE, "file", File, 560)
ASTKIND(NODE, "node", Node, 264)
#endif
