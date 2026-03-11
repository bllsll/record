SELECT t1.*
FROM flow_2025625_99 t1
WHERE t1.optime > 1750815000
  AND t1.optime < 1750820400
  AND t1.act = 1
  AND NOT EXISTS (
    SELECT 1
    FROM flow_2025625_99 t2
    WHERE t2.optime > 1750815000
      AND t2.optime < 1750820400
      AND t2.cycle = t1.cycle
      AND t2.act <> 1
  );

-- 生成删除表语句
SELECT Concat('DROP TABLE IF EXISTS ', table_name, ';') 
FROM information_schema.tables 
WHERE table_schema = 'agency' AND table_name LIKE 'agency_account_%';



172.31.15.95 改为172.31.6.171
%s/172.31.15.95/172.31.6.171/g


mysql -h cc-production-01-instance-1.chksg6c068sl.ap-south-1.rds.amazonaws.com -P 3306 -p'saedhji239WBSNJiwebjsWHKsbjmwq2' -ucc_game -A

redis-cli -h cc-production-single-001.cqznxb.ng.0001.aps1.cache.amazonaws.com -p 6379
ccgame:config:aviator2:poolcfg_1.json


ccgame:config:singleroom:pool:hash:104 10400

egrep GetBlackCfgId|CheckSatisfiedBlack.*76613797
SELECT * FROM log_asset_update_2025_02_17_48 WHERE game = 102 AND uid = 96526748;
SELECT * FROM log_game_circle_2025_02_17_48 WHERE uid = 96526748 AND game = 102;
SELECT * FROM log_game_circle_2025_02_16_48 WHERE uid = 96526748 AND game = 102;

SELECT * FROM log_game_circle_2025_02_17_29 WHERE uid = 10049029 AND game = 102;
SELECT * FROM log_game_circle_2025_02_17_29 WHERE uid = 10115729 AND game = 102;







SELECT * FROM log_game_circle_2025_02_17_29 WHERE uid = 10115729 AND game = 102;

select * from flow_20251023_4 where order_id = 7564249711834218330;

flow_2025630_69











SELECT * FROM log_asset_update_2025_11_03_28 WHERE uid = 42405528;

