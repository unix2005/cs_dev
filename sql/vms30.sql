drop index "org_idx";

alter table "d_org_info"
   drop primary key cascade;

drop table "d_org_info" cascade constraints;

/*==============================================================*/
/* Table: "d_org_info"                                          */
/*==============================================================*/
create table "d_org_info" (
   "org_id"             CHAR(36)              not null,
   "parent_org_id"      CHAR(36)              not null,
   "org_long_name"      VARCHAR2(128)         not null,
   "org_short_name"     VARCHAR2(128)         not null,
   "org_address"        VARCHAR2(256)         not null,
   "wgs84_lon"          NUMBER(12,6),
   "wgs84_lat"          NUMBER(12,6),
   "hy_lon"             NUMBER(12,6)          not null,
   "hy_lat"             NUMBER(12,6)          not null,
   "org_ranking"        CHAR(4)               not null,
   "org_show_id"        CHAR(1)               not null,
   "org_telephone"      VARCHAR2(16),
   "org_phone"          VARCHAR2(16),
   "contact_person"     VARCHAR2(32),
   "delete_flag"        CHAR(1)               not null,
   "begin_time"         DATE                  not null deferrable,
   "end_time"           DATE                  not null deferrable,
   "create_time"        DATE                 default SYSDATE  not null,
   "create_by"          VARCHAR2(64)          not null,
   "modify_time"        DATE,
   "modify_by"          VARCHAR2(64),
   "note"               VARCHAR2(128)
);

comment on table "d_org_info" is
'组织机构表
';

comment on column "d_org_info"."org_id" is
'组织机构编码';

comment on column "d_org_info"."parent_org_id" is
'父级组织机构编码，根节点父级编码为-1';

comment on column "d_org_info"."org_long_name" is
'组织机构全名';

comment on column "d_org_info"."org_short_name" is
'组织机构简称';

comment on column "d_org_info"."org_address" is
'组织机构所在地址';

comment on column "d_org_info"."wgs84_lon" is
'组织机构所在地经度';

comment on column "d_org_info"."wgs84_lat" is
'组织机构所在地维度';

comment on column "d_org_info"."hy_lon" is
'组织机构所在地华油偏转后经度';

comment on column "d_org_info"."hy_lat" is
'组织机构所在地华油偏转后纬度';

comment on column "d_org_info"."org_ranking" is
'组织机构对应的行政级别';

comment on column "d_org_info"."org_show_id" is
'组织机构树是否展示，0不展示，1展示';

comment on column "d_org_info"."org_telephone" is
'组织机构座机';

comment on column "d_org_info"."org_phone" is
'组织机构联系手机';

comment on column "d_org_info"."contact_person" is
'联系人';

comment on column "d_org_info"."delete_flag" is
'失效标志位，0 生效，1失效';

comment on column "d_org_info"."begin_time" is
'功能生效时间';

comment on column "d_org_info"."end_time" is
'功能失效时间';

comment on column "d_org_info"."create_time" is
'创建时间';

comment on column "d_org_info"."create_by" is
'创建人';

comment on column "d_org_info"."modify_time" is
'修改时间';

comment on column "d_org_info"."modify_by" is
'修改人';

comment on column "d_org_info"."note" is
'备注';

alter table "d_org_info"
   add constraint PK_D_ORG_INFO primary key ("org_id");

/*==============================================================*/
/* Index: "org_idx"                                             */
/*==============================================================*/
create unique index "org_idx" on "d_org_info" (
   "org_id" ASC,
   "delete_flag" ASC
);

alter table "d_vehicleinfo"
   drop primary key cascade;

drop table "d_vehicleinfo" cascade constraints;

/*==============================================================*/
/* Table: "d_vehicleinfo"                                       */
/*==============================================================*/
create table "d_vehicleinfo" (
   "vehicle_id"         CHAR(32 char)         not null,
   "lis_plate"          CHAR(20 char)         not null,
   "lis_plate_color"    CHAR(2 char)          not null,
   "org_id"             CHAR(36 char)         not null,
   "m_driver_empid"     CHAR(32 char),
   "s_driver_empid"     CHAR(32 char),
   "sup_empid"          CHAR(32),
   "vehicle_type_id"    CHAR(36 char)         not null,
   "vehicle_use"        CHAR(36 char)         not null,
   "scrapped_flag"      NUMBER(1)            default 0  not null,
   "erp_code"           CHAR(36 char),
   "user_defined_code"  CHAR(36 char),
   "dbh_key_id"         CHAR(36),
   DATA_FROM            CHAR(1 char)         default 'V',
   "create_time"        DATE                 default SYSDATE  not null,
   "create_by"          CHAR(32 char)         not null,
   "modify_time"        DATE,
   "modify_by"          CHAR(32 char),
   "note"               VARCHAR2(512)
);

comment on table "d_vehicleinfo" is
'车辆基本信息表
此表与 车辆报警开关(d_ve_alarm_switch)、车辆限速值表(d_ve_over_speed)联动，新增车辆、删除车辆同步处理这两个表。
删除时与车辆区域限速表（d_ve_area_ovspeed）、车辆进出区域开关表（d_ve_area_switch）、车辆终端绑定信息表（d_vehicle_term）、车辆安装摄像头记录表（d_vehicle_camera）联动，同步删除这些表数据';

comment on column "d_vehicleinfo"."vehicle_id" is
'车辆ID，车牌号+颜色 最后整-V的为外单位导入车辆';

comment on column "d_vehicleinfo"."lis_plate" is
'车牌号';

comment on column "d_vehicleinfo"."lis_plate_color" is
'车牌颜色';

comment on column "d_vehicleinfo"."org_id" is
'车辆归属的组织机构';

comment on column "d_vehicleinfo"."m_driver_empid" is
'驾驶员employee_id';

comment on column "d_vehicleinfo"."s_driver_empid" is
'副驾驶employee_id';

comment on column "d_vehicleinfo"."sup_empid" is
'押运员employee_id';

comment on column "d_vehicleinfo"."vehicle_type_id" is
'车辆类型';

comment on column "d_vehicleinfo"."vehicle_use" is
'车辆用途，字典项';

comment on column "d_vehicleinfo"."scrapped_flag" is
'是否报废';

comment on column "d_vehicleinfo"."erp_code" is
'ERP系统标志字段';

comment on column "d_vehicleinfo"."user_defined_code" is
'用户自定义编号';

comment on column "d_vehicleinfo"."dbh_key_id" is
'数据仓库主键';

comment on column "d_vehicleinfo".DATA_FROM is
'#数据来源：V-车辆终端；
F-地区平台';

comment on column "d_vehicleinfo"."create_time" is
'RecordTime:记录时间';

comment on column "d_vehicleinfo"."create_by" is
'Recordor:记录人';

comment on column "d_vehicleinfo"."modify_time" is
'ModifyTime:修改时间';

comment on column "d_vehicleinfo"."modify_by" is
'Modifier:修改人';

alter table "d_vehicleinfo"
   add constraint PK_D_VEHICLEINFO primary key ("vehicle_id");
