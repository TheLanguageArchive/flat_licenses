<?php
// defining license db table name
define('FLAT_LICENSES_DB', 'flat_licenses');

/**
 * db error was found, send drupal message to show what
 * went wrong
 *
 * @param Exception $e
 */
function flat_licenses_db_error(Exception $e)
{

    if ($e->errorInfo[0] == '42P01') {
        drupal_set_message(t('Flat License custom labels table not found. Try running
         <a href="@update_url">update.php</a>.', ['@update_url' => url('update.php')]), 'error');
    } else {
        drupal_set_message($e->getMessage(), 'error');
    }
}

/**
 * Query db to fetch the licenses
 *
 * @return array
 */
function flat_licenses_db_get_values()
{

    $query = db_select(FLAT_LICENSES_DB, 'fll');
    $query->fields('fll');

    try {
        $result  = $query->execute();
        $data    = $result->fetchAll(PDO::FETCH_ASSOC);
        $records = [];

        foreach ($data as $item) {
            $records[$item['id']] = $item;
        }
    } catch (Exception $e) {
        flat_licenses_db_error($e);
        $records = [];
    }

    return $records;
}

/**
 * Commit licenses to db
 * WARNING: this will clear the old values to commit the new ones
 *
 * @param array $licenses
 *
 * @return boolean
 */
function flat_licenses_db_commit_values($licenses)
{

    $error = false;

    // purge labels
    db_delete(FLAT_LICENSES_DB)->execute();

    if (count($licenses) > 0) {
        $insert = db_insert(FLAT_LICENSES_DB)->fields([
            'label',
            'url',
            'pid',
        ]);

        foreach ($licenses as $id => $licenses) {
            foreach ($licenses as $license) {
                $insert->values([

                    'label' => $license['label'],
                    'url'  => $license['url'],
                    'pid'  => $license['pid'],

                ]);
            }
        }

        try {
            $insert->execute();
        } catch (Exception $e) {
            flat_licenses_db_error($e);
            $error = true;
        }
    }

    return $error;
}
