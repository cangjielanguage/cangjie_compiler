// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.
// ASTKIND(ast kind, ast value for human reading, ast node, ast size for notify checking related code)
#ifdef ASTKIND
ASTKIND(ANNOTATION, "annotation", Annotation, 512)
ASTKIND(MODIFIER, "modifier", Modifier, 264)
ASTKIND(DECL, "*decl", Decl, 728) // begin of subtypes of Decl.
ASTKIND(MAIN_DECL, "main_decl", MainDecl, 752)
ASTKIND(FUNC_DECL, "func_decl", FuncDecl, 824)
ASTKIND(MACRO_DECL, "macro_decl", MacroDecl, 744)
ASTKIND(CLASS_LIKE_DECL, "class_like_decl", ClassLikeDecl, 824) // begin: subtypes of ClassLikeDecl and InheritableDecl.
ASTKIND(CLASS_DECL, "class_decl", ClassDecl, 840)
ASTKIND(INTERFACE_DECL, "interface_decl", InterfaceDecl, 832) // end: subtypes of ClassLikeDecl.
ASTKIND(EXTEND_DECL, "extend_decl", ExtendDecl, 888)
ASTKIND(ENUM_DECL, "enum_decl", EnumDecl, 928)
ASTKIND(STRUCT_DECL, "struct_decl", StructDecl, 792) // end of subtypes of InheritableDecl.
ASTKIND(TYPE_ALIAS_DECL, "type_alias_decl", TypeAliasDecl, 760)
ASTKIND(PRIMARY_CTOR_DECL, "primary_ctor_decl", PrimaryCtorDecl, 744)
ASTKIND(BUILTIN_DECL, "builtin_decl", BuiltInDecl, 736)
ASTKIND(VAR_DECL, "var_decl", VarDecl, 808) // begin of subtypes of VarDecl and VarDeclAbstract.
ASTKIND(PROP_DECL, "prop_decl", PropDecl, 896)
ASTKIND(MACRO_EXPAND_PARAM, "macro_expand_param", MacroExpandParam, 1608)
ASTKIND(FUNC_PARAM, "func_param", FuncParam, 872)                                // end of subtypes of VarDecl.
ASTKIND(VAR_WITH_PATTERN_DECL, "var_with_pattern_decl", VarWithPatternDecl, 800) // end of subtypes of VarDeclAbstract.
ASTKIND(GENERIC_PARAM_DECL, "generic_param_decl", GenericParamDecl, 752)
ASTKIND(PACKAGE_DECL, "package_decl", PackageDecl, 736)
ASTKIND(MACRO_EXPAND_DECL, "macro_expand_decl", MacroExpandDecl, 1464)
ASTKIND(INVALID_DECL, "invalid_decl", InvalidDecl, 728) // end of subtypes of Decl.
ASTKIND(PATTERN, "*pattern", Pattern, 264)              // begin of subTypes of Pattern.
ASTKIND(VAR_PATTERN, "var_pattern", VarPattern, 280)
ASTKIND(CONST_PATTERN, "const_pattern", ConstPattern, 280)
ASTKIND(TUPLE_PATTERN, "tuple_pattern", TuplePattern, 360)
ASTKIND(ENUM_PATTERN, "enum_pattern", EnumPattern, 368)
ASTKIND(VAR_OR_ENUM_PATTERN, "var_or_enum_pattern", VarOrEnumPattern, 352)
ASTKIND(TYPE_PATTERN, "type_pattern", TypePattern, 328)
ASTKIND(EXCEPT_TYPE_PATTERN, "except_type_pattern", ExceptTypePattern, 360)
ASTKIND(COMMAND_TYPE_PATTERN, "effect_type_pattern", CommandTypePattern, 360)
ASTKIND(WILDCARD_PATTERN, "wildcard_pattern", WildcardPattern, 264)
ASTKIND(INVALID_PATTERN, "invalid_pattern", InvalidPattern, 264) // end of subtypes of Pattern.
ASTKIND(TYPE, "*type", Type, 376)                                // begin of subtypes of Type.
ASTKIND(REF_TYPE, "ref_type", RefType, 560)
ASTKIND(QUALIFIED_TYPE, "qualified_type", QualifiedType, 568)
ASTKIND(OPTION_TYPE, "option_type", OptionType, 424)
ASTKIND(CONSTANT_TYPE, "constant_type", ConstantType, 408)
ASTKIND(VARRAY_TYPE, "varray_type", VArrayType, 456)
ASTKIND(PRIMITIVE_TYPE, "primitive_type", PrimitiveType, 416)
ASTKIND(PAREN_TYPE, "paren_type", ParenType, 424)
ASTKIND(FUNC_TYPE, "func_type", FuncType, 472)
ASTKIND(TUPLE_TYPE, "tuple_type", TupleType, 464)
ASTKIND(THIS_TYPE, "this_type", ThisType, 376)
ASTKIND(INVALID_TYPE, "invalid_type", InvalidType, 376) // end of subtypes of Type.
ASTKIND(EXPR, "*expr", Expr, 432)                       // begin of subtypes of Expr.
ASTKIND(WILDCARD_EXPR, "wildcard_expr", WildcardExpr, 432)
ASTKIND(CALL_EXPR, "call_expr", CallExpr, 592)
ASTKIND(PAREN_EXPR, "paren_expr", ParenExpr, 480)
ASTKIND(MEMBER_ACCESS, "member_access_expr", MemberAccess, 768) // begin of subtypes of NameReferenceExpr.
ASTKIND(REF_EXPR, "ref_expr", RefExpr, 672)                     // end of subtypes of NameReferenceExpr.
ASTKIND(OPTIONAL_EXPR, "optional_expr", OptionalExpr, 464)
ASTKIND(OPTIONAL_CHAIN_EXPR, "optional_chain_expr", OptionalChainExpr, 432)
ASTKIND(PRIMITIVE_TYPE_EXPR, "primitive_type_expr", PrimitiveTypeExpr, 432)
ASTKIND(RETURN_EXPR, "return_expr", ReturnExpr, 464)
ASTKIND(LIT_CONST_EXPR, "lit_const_expr", LitConstExpr, 544)
ASTKIND(INTERPOLATION_EXPR, "interpolation_expr", InterpolationExpr, 496)
ASTKIND(STR_INTERPOLATION_EXPR, "str_interpolation_expr", StrInterpolationExpr, 512)
ASTKIND(ASSIGN_EXPR, "assign_expr", AssignExpr, 480) // begin of OverloadableExpr.
ASTKIND(UNARY_EXPR, "unary_expr", UnaryExpr, 464)
ASTKIND(BINARY_EXPR, "binary_expr", BinaryExpr, 464)
ASTKIND(INC_OR_DEC_EXPR, "inc_or_dec_expr", IncOrDecExpr, 464)
ASTKIND(SUBSCRIPT_EXPR, "subscript_expr", SubscriptExpr, 528) // end of OverloadableExpr.
ASTKIND(IS_EXPR, "is_expr", IsExpr, 464)
ASTKIND(AS_EXPR, "as_expr", AsExpr, 464)
ASTKIND(RANGE_EXPR, "range_expr", RangeExpr, 512)
ASTKIND(ARRAY_LIT, "array_lit_expr", ArrayLit, 528)
ASTKIND(ARRAY_EXPR, "array_expr", ArrayExpr, 544)
ASTKIND(POINTER_EXPR, "pointer_expr", PointerExpr, 448)
ASTKIND(TUPLE_LIT, "tuple_lit_expr", TupleLit, 528)
ASTKIND(MATCH_EXPR, "match_expr", MatchExpr, 576)
ASTKIND(BLOCK, "block", Block, 512)
ASTKIND(IF_EXPR, "if_expr", IfExpr, 544)
ASTKIND(LET_PATTERN_DESTRUCTOR, "let_pattern_destructor", LetPatternDestructor, 512)
ASTKIND(TOKEN_PART, "token_part", TokenPart, 448)
ASTKIND(QUOTE_EXPR, "quote_expr", QuoteExpr, 512)
ASTKIND(TRY_EXPR, "try_expr", TryExpr, 752)
ASTKIND(WHILE_EXPR, "while_expr", WhileExpr, 512)
ASTKIND(JUMP_EXPR, "jump_expr", JumpExpr, 448)
ASTKIND(LAMBDA_EXPR, "lambda_expr", LambdaExpr, 464)
ASTKIND(TRAIL_CLOSURE_EXPR, "trailing_closure_expr", TrailingClosureExpr, 496)
ASTKIND(FOR_IN_EXPR, "for_in_expr", ForInExpr, 560)
ASTKIND(DO_WHILE_EXPR, "do_while_expr", DoWhileExpr, 528)
ASTKIND(TYPE_CONV_EXPR, "type_conv_expr", TypeConvExpr, 496)
ASTKIND(THROW_EXPR, "throw_expr", ThrowExpr, 464)
ASTKIND(PERFORM_EXPR, "perform_expr", PerformExpr, 464)
ASTKIND(RESUME_EXPR, "resume_expr", ResumeExpr, 528)
ASTKIND(SPAWN_EXPR, "spawn_expr", SpawnExpr, 512)
ASTKIND(SYNCHRONIZED_EXPR, "synchronized_expr", SynchronizedExpr, 512)
ASTKIND(MACRO_EXPAND_EXPR, "macro_expand_expr", MacroExpandExpr, 1312)
ASTKIND(IF_AVAILABLE_EXPR, "if_available_expr", IfAvailableExpr, 448)
ASTKIND(INVALID_EXPR, "invalid_expr", InvalidExpr, 464) // end of subtypes of Expr.
ASTKIND(GENERIC, "generic", Generic, 400)
ASTKIND(GENERIC_CONSTRAINT, "generic_constraint", GenericConstraint, 384)
ASTKIND(MATCH_CASE, "match_case", MatchCase, 360)
ASTKIND(MATCH_CASE_OTHER, "match_case_other", MatchCaseOther, 296)
ASTKIND(FUNC_ARG, "func_arg", FuncArg, 416)
ASTKIND(FUNC_PARAM_LIST, "func_param_list", FuncParamList, 344)
ASTKIND(FUNC_BODY, " func_body", FuncBody, 448)
ASTKIND(STRUCT_BODY, "struct_body", StructBody, 328)
ASTKIND(CLASS_BODY, "class_body", ClassBody, 328)
ASTKIND(INTERFACE_BODY, "interface_body", InterfaceBody, 328)
ASTKIND(DUMMY_BODY, "dummy_body", DummyBody, 256)
ASTKIND(IMPORT_CONTENT, "import_content", ImportContent, 600)
ASTKIND(IMPORT_SPEC, "import_spec", ImportSpec, 912)
ASTKIND(PACKAGE_SPEC, "package_spec", PackageSpec, 464)
ASTKIND(PACKAGE, "package", Package, 512)
ASTKIND(FEATURE_ID, "feature_id", FeatureId, 304)
ASTKIND(FEATURES_DIRECTIVE, "features_directive", FeaturesDirective, 304)
ASTKIND(FILE, "file", File, 552)
ASTKIND(NODE, "node", Node, 256)
#endif
